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

static uint32_t noiseState = 1;

/* Recorder: one ADC2 conversion every recDiv timer periods */
static uint16_t recBuf[TESTSIGNAL_REC_MAX];
static uint16_t recDacBuf[TESTSIGNAL_REC_MAX];
static uint32_t recDiv;
static volatile uint32_t recCount;
static volatile uint32_t recIdx;
static uint32_t recTick;
static bool adcReady;

static void tmr6Isr(const void *arg)
{
    (void)arg;

    if (tmr_interrupt_flag_get(TMR6, TMR_OVF_FLAG) == RESET)
        return;

    tmr_flag_clear(TMR6, TMR_OVF_FLAG);

    uint32_t idx = phase >> (32 - TABLE_BITS);
    int32_t value;

    if (shape == TESTSIGNAL_SQUARE) {
        value = (idx < (TABLE_SIZE / 2)) ? amplitude : -amplitude;
    } else if (shape == TESTSIGNAL_NOISE) {
        /* xorshift32, Marsaglia, "Xorshift RNGs", J. Stat. Soft. 2003 */
        noiseState ^= noiseState << 13;
        noiseState ^= noiseState >> 17;
        noiseState ^= noiseState << 5;
        value = ((int32_t)(int16_t)(noiseState >> 16) * amplitude) >> 15;
    } else {
        value = (sineTable[idx] * amplitude) >> 15;
    }

    phase += phaseStep;
    uint16_t dacValue = (uint16_t)(DAC_MID + value);
    dac_1_data_set(DAC1_12BIT_RIGHT, dacValue);

    /*
     * Each conversion is started one sample period before it is read, much
     * longer than the conversion time.
     */
    if (recIdx >= recCount)
        return;
    if (++recTick < recDiv)
        return;

    recTick = 0;
    if (adc_flag_get(ADC2, ADC_CCE_FLAG) == SET) {
        recDacBuf[recIdx] = dacValue;
        recBuf[recIdx++] = adc_ordinary_conversion_data_get(ADC2);
    }
    if (recIdx < recCount)
        adc_ordinary_software_trigger_enable(ADC2, TRUE);
}

static void hwInit(void)
{
    gpio_init_type pin;

    for (size_t i = 0; i < TABLE_SIZE; i++)
        sineTable[i] =
            (int16_t)(32767.0f * sinf(2.0f * 3.14159265f * i / TABLE_SIZE));

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

static void adcInit(void)
{
    adc_base_config_type cfg;

    crm_periph_clock_enable(CRM_ADC2_PERIPH_CLOCK, TRUE);

    adc_base_default_para_init(&cfg);
    cfg.sequence_mode = FALSE;
    cfg.repeat_mode = FALSE;
    cfg.data_align = ADC_RIGHT_ALIGNMENT;
    cfg.ordinary_channel_length = 1;
    adc_base_config(ADC2, &cfg);
    adc_ordinary_conversion_trigger_set(ADC2, ADC12_ORDINARY_TRIG_SOFTWARE,
                                        TRUE);
    adc_enable(ADC2, TRUE);

    adc_calibration_init(ADC2);
    while (adc_calibration_init_status_get(ADC2) == SET)
        ;
    adc_calibration_start(ADC2);
    while (adc_calibration_status_get(ADC2) == SET)
        ;

    adcReady = true;
}

static void analogPin(uint8_t channel)
{
    gpio_init_type pin;

    /* Channels 0-7 are PA0-PA7 */
    if (channel > 7)
        return;

    gpio_default_para_init(&pin);
    pin.gpio_pins = 1U << channel;
    pin.gpio_mode = GPIO_MODE_ANALOG;
    gpio_init(GPIOA, &pin);
}

int testsignal_record(uint8_t channel, uint32_t count, uint32_t div,
                      const uint16_t **samples, const uint16_t **dacSamples)
{
    if ((channel > 15) || (count == 0) || (count > TESTSIGNAL_REC_MAX)
        || (div == 0) || (div > SAMPLE_RATE / 1000))
        return -EINVAL;

    if (!hwReady)
        hwInit();
    if (!adcReady)
        adcInit();

    analogPin(channel);
    /* Short enough to fit a 48 kHz sample period at any ADC clock */
    adc_ordinary_channel_set(ADC2, (adc_channel_select_type)channel, 1,
                             ADC_SAMPLETIME_71_5);

    /* Without a test signal the timer runs silent, for the timing */
    bool silent = (shape == TESTSIGNAL_OFF);
    if (silent) {
        amplitude = 0;
        shape = TESTSIGNAL_SINE;
        tmr_counter_enable(TMR6, TRUE);
    }

    recDiv = div;
    recTick = 0;
    recIdx = 0;
    adc_ordinary_software_trigger_enable(ADC2, TRUE);
    recCount = count;

    while (recIdx < recCount)
        k_msleep(10);

    if (silent)
        testsignal_start(TESTSIGNAL_OFF, 0, 0);

    *samples = recBuf;
    *dacSamples = recDacBuf;

    return 0;
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
