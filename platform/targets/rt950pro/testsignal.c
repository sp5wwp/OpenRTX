/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <math.h>
#include <soc.h>
#include <stdbool.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include "testsignal.h"

/*
 * Bench test signal on DAC1 (PA4), the output the stock firmware streams its
 * tones and AFSK from (V0.29, 0x080064D0). One 12-bit sample is written per
 * TMR6 overflow interrupt, at 48 kHz.
 *
 * Timer clock: SYSCLK 120 MHz, APB1 /2, so the APB1 timers run at 120 MHz
 * (AT32F403A reference manual, clock tree).
 */

#define TIMER_CLOCK 120000000UL
#define SAMPLE_RATE 48000UL
#define DAC_MID 2048
#define DAC_MAX_AMP 2047
#define TABLE_BITS 8
#define TABLE_SIZE (1 << TABLE_BITS)
#define TMR6_IRQ_PRIO 2

static int16_t sineTable[TABLE_SIZE];
static volatile uint32_t phase;
static volatile uint32_t phaseStep;
static volatile int32_t amplitude;
static volatile enum testsignalShape shape;
static bool hwReady;

static void tmr6Isr(const void *arg)
{
    (void)arg;

    if (tmr_interrupt_flag_get(TMR6, TMR_OVF_FLAG) == RESET)
        return;

    tmr_flag_clear(TMR6, TMR_OVF_FLAG);

    uint32_t idx = phase >> (32 - TABLE_BITS);
    int32_t value;

    if (shape == TESTSIGNAL_SQUARE)
        value = (idx < (TABLE_SIZE / 2)) ? amplitude : -amplitude;
    else
        value = (sineTable[idx] * amplitude) >> 15;

    phase += phaseStep;
    dac_1_data_set(DAC1_12BIT_RIGHT, (uint16_t)(DAC_MID + value));
}

static void hwInit(void)
{
    gpio_init_type pin;

    for (size_t i = 0; i < TABLE_SIZE; i++)
        sineTable[i] = (int16_t)(32767.0f * sinf(2.0f * 3.14159265f * i
                                                  / TABLE_SIZE));

    crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_DAC_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_TMR6_PERIPH_CLOCK, TRUE);

    gpio_default_para_init(&pin);
    pin.gpio_pins = GPIO_PINS_4;
    pin.gpio_mode = GPIO_MODE_ANALOG;
    gpio_init(GPIOA, &pin);

    /* Output buffer off, as Ferrite does on this pin (hal/audio.rs) */
    dac_output_buffer_enable(DAC1_SELECT, FALSE);
    dac_trigger_enable(DAC1_SELECT, FALSE);
    dac_1_data_set(DAC1_12BIT_RIGHT, DAC_MID);
    dac_enable(DAC1_SELECT, TRUE);

    tmr_base_init(TMR6, (TIMER_CLOCK / SAMPLE_RATE) - 1, 0);
    tmr_cnt_dir_set(TMR6, TMR_COUNT_UP);
    tmr_interrupt_enable(TMR6, TMR_OVF_INT, TRUE);

    IRQ_CONNECT(TMR6_GLOBAL_IRQn, TMR6_IRQ_PRIO, tmr6Isr, NULL, 0);
    irq_enable(TMR6_GLOBAL_IRQn);

    hwReady = true;
}

int testsignal_start(enum testsignalShape newShape, uint32_t hz, uint32_t amp)
{
    if (amp > DAC_MAX_AMP)
        return -EINVAL;
    if ((newShape != TESTSIGNAL_OFF) && (hz >= (SAMPLE_RATE / 2)))
        return -EINVAL;

    if (!hwReady)
        hwInit();

    tmr_counter_enable(TMR6, FALSE);

    if (newShape == TESTSIGNAL_OFF) {
        shape = TESTSIGNAL_OFF;
        dac_1_data_set(DAC1_12BIT_RIGHT, DAC_MID);
        return 0;
    }

    phase = 0;
    phaseStep = (uint32_t)(((uint64_t)hz << 32) / SAMPLE_RATE);
    amplitude = (int32_t)amp;
    shape = newShape;

    tmr_counter_enable(TMR6, TRUE);

    return 0;
}
