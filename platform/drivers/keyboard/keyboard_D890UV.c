/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

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
 * checked on the radio except for P1 and P2 (stock codes 19 and 20).
 */
static const keyboard_t keyMap[KBD_ROWS][KBD_COLUMNS] = {
    /* KBD_ROW1, stock codes 2 3 4 5 6 7 1 11 */
    { KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_0, KEY_STAR },
    /* KBD_ROW2, stock codes 13 15 16 8 9 10 12 14 */
    { KEY_ENTER, KEY_UP, KEY_DOWN, KEY_7, KEY_8, KEY_9, KEY_HASH, KEY_ESC },
    /* KBD_ROW3, stock codes 19 20: P1 and P2 */
    { KEY_F4, KEY_F5, 0, 0, 0, 0, 0, 0 },
};

/* Knob direction for each (previous << 2 | current) Gray code transition */
static const int8_t knobStep[16] = {
    0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0,
};

static uint8_t knobPrev;

static uint8_t readKnob()
{
    return (gpio_readPin(KNOB_B) << 1) | gpio_readPin(KNOB_A);
}

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
}

void kbd_terminate()
{
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

    uint8_t knob = readKnob();
    if (knob != knobPrev) {
        int8_t dir = knobStep[(knobPrev << 2) | knob];
        if (dir > 0)
            keys |= KNOB_RIGHT;
        else if (dir < 0)
            keys |= KNOB_LEFT;

        knobPrev = knob;
    }

    lastKeys = keys;
    return keys;
}
