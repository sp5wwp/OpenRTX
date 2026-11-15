/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdbool.h>
#include <stdint.h>
#include "peripherals/gpio.h"
#include "interfaces/delays.h"
#include "interfaces/keyboard.h"
#include "hwconfig.h"

/*
 * The keypad is a 3x8 matrix: the columns are the display data lines D0-D7,
 * driven low one at a time, the rows are three inputs with pull-ups. Layout
 * recovered from the key scan routine of the stock AnyTone D890UV firmware
 * V1.05 (D890UV_V1.05_20260521.CDD, function at 0x08101260).
 *
 * The display is deselected (CS high) whenever the scan runs, both happen in
 * the UI thread, so driving the shared lines here is harmless.
 */

#define KBD_COLUMNS 8
#define KBD_ROWS 3

/*
 * Keys indexed by [row][column], with the stock key code of each position,
 * checked on the radio (P1 and P2 too, row 3).
 */
static const keyboard_t keyMap[KBD_ROWS][KBD_COLUMNS] = {
    /* KBD_ROW1, stock codes 2 3 4 5 6 7 1 11 */
    { KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_0, KEY_STAR },
    /* KBD_ROW2, stock codes 13 15 16 8 9 10 12 14 */
    { KEY_ENTER, KEY_UP, KEY_DOWN, KEY_7, KEY_8, KEY_9, KEY_HASH, KEY_ESC },
    /* KBD_ROW3, stock codes 19 20: P1 and P2, used as left and right */
    { KEY_LEFT, KEY_RIGHT, 0, 0, 0, 0, 0, 0 },
};

/* Knob direction for each (previous << 2 | current) Gray code transition */
static const int8_t knobStep[16] = {
    0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0,
};

/*
 * The knob is decoded in EXTI interrupts on both edges of both lines: polled
 * at the 40Hz keypad scan, a quick turn skipped states and lost steps.
 * knobCount holds the signed number of transitions, knobUsed the part already
 * reported as steps.
 */
#define KNOB_TRANSITIONS_PER_STEP 2
#define KNOB_MAX_BACKLOG 8

static volatile uint8_t knobPrev;
volatile int32_t kbd_knobCount;
static int32_t knobUsed;
static bool knobGap;

static uint8_t readKnob()
{
    return (gpio_readPin(KNOB_B) << 1) | gpio_readPin(KNOB_A);
}

static void knobIrq()
{
    EXTI->RPR1 = (1u << 8) | (1u << 9);
    EXTI->FPR1 = (1u << 8) | (1u << 9);

    uint8_t knob = readKnob();
    kbd_knobCount += knobStep[(knobPrev << 2) | knob];
    knobPrev = knob;
}

void _Z16EXTI8_IRQHandlerv()
{
    knobIrq();
}

void _Z16EXTI9_IRQHandlerv()
{
    knobIrq();
}

/*
 * Release debounce: a key counts as released after KEY_RELEASE_SCANS scans
 * without contact. A contact bounce seen by one scan made a single press
 * count twice.
 */
#define KEY_RELEASE_SCANS 2

static keyboard_t heldKeys;
static uint8_t releaseScans[32];

static inline GPIO_TypeDef *portOf(const void *port, const uint8_t pin)
{
    (void)pin;
    return (GPIO_TypeDef *)port;
}

static inline uint8_t pinOf(const void *port, const uint8_t pin)
{
    (void)port;
    return pin;
}

/*
 * Set the column lines in a single write: bit n of 'low' drives column n low,
 * all the others high. D0-D7 are consecutive pins of one port.
 */
static void setColumns(const uint8_t low)
{
    const uint8_t shift = pinOf(LCD_D0);
    const uint32_t clr = (uint32_t)low << shift;
    const uint32_t set = (uint32_t)(uint8_t)~low << shift;

    portOf(LCD_D0)->BSRR = set | (clr << 16);
}

static uint8_t readRows()
{
    return (gpio_readPin(KBD_ROW1) == 0 ? 1 : 0)
         | (gpio_readPin(KBD_ROW2) == 0 ? 2 : 0)
         | (gpio_readPin(KBD_ROW3) == 0 ? 4 : 0);
}

void kbd_init()
{
    gpio_setMode(KBD_ROW1, INPUT_PULL_UP);
    gpio_setMode(KBD_ROW2, INPUT_PULL_UP);
    gpio_setMode(KBD_ROW3, INPUT_PULL_UP);

    gpio_setMode(SIDE_KEY1, INPUT_PULL_UP);
    gpio_setMode(SIDE_KEY2, INPUT_PULL_UP);
    gpio_setMode(ALARM_KEY, INPUT_PULL_UP);

    gpio_setMode(KNOB_A, INPUT);
    gpio_setMode(KNOB_B, INPUT);
    knobPrev = readKnob();

    /* PC8 and PC9 on EXTI lines 8 and 9, both edges */
    RCC->APB3ENR |= RCC_APB3ENR_SYSCFGEN;
    EXTI->EXTICR[2] = (EXTI->EXTICR[2] & ~0xFFFFu) | 0x0202u;
    EXTI->RTSR1 |= (1u << 8) | (1u << 9);
    EXTI->FTSR1 |= (1u << 8) | (1u << 9);
    EXTI->RPR1 = (1u << 8) | (1u << 9);
    EXTI->FPR1 = (1u << 8) | (1u << 9);
    EXTI->IMR1 |= (1u << 8) | (1u << 9);
    NVIC_SetPriority(EXTI8_IRQn, 12);
    NVIC_SetPriority(EXTI9_IRQn, 12);
    NVIC_EnableIRQ(EXTI8_IRQn);
    NVIC_EnableIRQ(EXTI9_IRQn);
}

void kbd_terminate()
{
    NVIC_DisableIRQ(EXTI8_IRQn);
    NVIC_DisableIRQ(EXTI9_IRQn);
    EXTI->IMR1 &= ~((1u << 8) | (1u << 9));
}

/* Result of the last scan, for kbd_lastKeys() */
static volatile keyboard_t lastKeys;

keyboard_t kbd_lastKeys()
{
    return lastKeys;
}

keyboard_t kbd_getKeys()
{
    keyboard_t keys = 0;

    for (uint8_t col = 0; col < KBD_COLUMNS; col++) {
        setColumns(1u << col);
        delayUs(10);

        uint8_t rows = readRows();
        for (uint8_t row = 0; row < KBD_ROWS; row++) {
            if (rows & (1u << row))
                keys |= keyMap[row][col];
        }
    }

    /* Leave all data lines high, as an idle bus */
    setColumns(0);

    if (gpio_readPin(SIDE_KEY1) == 0)
        keys |= KEY_MONI;
    if (gpio_readPin(SIDE_KEY2) == 0)
        keys |= KEY_F1;
    if (gpio_readPin(ALARM_KEY) == 0)
        keys |= KEY_F3;

    /* Debounce the key releases */
    for (uint8_t k = 0; k < 32; k++) {
        keyboard_t mask = (keyboard_t)1 << k;
        if (keys & mask) {
            heldKeys |= mask;
            releaseScans[k] = 0;
        } else if (heldKeys & mask) {
            releaseScans[k] += 1;
            if (releaseScans[k] >= KEY_RELEASE_SCANS)
                heldKeys &= ~mask;
        }
    }
    keys = heldKeys;

    /*
     * One knob step per scan, and a scan without knob bits after each: input
     * events are sent on changes only, so steps in a row would merge.
     */
    if (knobGap) {
        knobGap = false;
    } else {
        int32_t steps = (kbd_knobCount - knobUsed) / KNOB_TRANSITIONS_PER_STEP;
        // Drop the excess of a fast spin rather than step on for seconds
        if (steps > KNOB_MAX_BACKLOG) {
            knobUsed += (steps - KNOB_MAX_BACKLOG) * KNOB_TRANSITIONS_PER_STEP;
            steps = KNOB_MAX_BACKLOG;
        } else if (steps < -KNOB_MAX_BACKLOG) {
            knobUsed += (steps + KNOB_MAX_BACKLOG) * KNOB_TRANSITIONS_PER_STEP;
            steps = -KNOB_MAX_BACKLOG;
        }
        if (steps > 0) {
            keys |= KNOB_RIGHT;
            knobUsed += KNOB_TRANSITIONS_PER_STEP;
            knobGap = true;
        } else if (steps < 0) {
            keys |= KNOB_LEFT;
            knobUsed -= KNOB_TRANSITIONS_PER_STEP;
            knobGap = true;
        }
    }

    lastKeys = keys;
    return keys;
}
