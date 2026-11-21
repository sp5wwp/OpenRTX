/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <math.h>
#include <soc.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <drivers/audio/adc_at32.h>
#include <drivers/audio/dac_at32.h>
#include "testsignal.h"

/*
 * Bench signals, through the per-sample hooks of the DAC driver (48 kHz):
 * - a test signal generator, added to the stream being played, if any;
 * - a recorder of an ADC channel next to the final DAC output. It is hooked
 *   at its first use, after the canceller (canceller.c), so that it records
 *   what is really output. PA2 is recorded from the ADC driver, so as the
 *   canceller sees it; other channels are converted on ADC2.
 */

#define SAMPLE_RATE DAC_AT32_RATE
#define TABLE_BITS 8
#define TABLE_SIZE (1 << TABLE_BITS)
/* PA2, read by the ADC driver */
#define DRIVER_CHANNEL 2

static int16_t sineTable[TABLE_SIZE];
static volatile uint32_t phase;
static volatile uint32_t phaseStep;
static volatile int32_t amplitude;
static volatile enum testsignalShape shape;
static uint32_t noiseState = 1;

/*
 * Recorder: one sample every recDiv sample periods. The buffers are allocated
 * by each recording and kept until the next one, for the host to read them:
 * the bench costs no memory until it is used.
 */
static uint16_t *recBuf;
static uint16_t *recDacBuf;
static uint32_t recDiv;
static volatile uint32_t recCount;
static volatile uint32_t recIdx;
static uint32_t recTick;
static bool recFromDriver;
static bool recHooked;

/* Interrupt path: in RAM and without library calls, see dac_at32.c */
static AUDIO_ISR_CODE int32_t generate(void)
{
    uint32_t idx = phase >> (32 - TABLE_BITS);
    int32_t value = 0;

    switch (shape) {
        case TESTSIGNAL_SINE:
            value = (sineTable[idx] * amplitude) >> 15;
            break;
        case TESTSIGNAL_SQUARE:
            value = (idx < (TABLE_SIZE / 2)) ? amplitude : -amplitude;
            break;
        case TESTSIGNAL_NOISE:
            /* xorshift32, Marsaglia, "Xorshift RNGs", J. Stat. Soft. 2003 */
            noiseState ^= noiseState << 13;
            noiseState ^= noiseState >> 17;
            noiseState ^= noiseState << 5;
            value = ((int32_t)(int16_t)(noiseState >> 16) * amplitude) >> 15;
            break;
        default:
            break;
    }

    phase += phaseStep;

    return value;
}

static AUDIO_ISR_CODE int32_t generatorHook(int32_t sample)
{
    return sample + generate();
}

static AUDIO_ISR_CODE int32_t recorderHook(int32_t x)
{
    if (recIdx >= recCount)
        return x;
    if (++recTick < recDiv)
        return x;

    recTick = 0;
    if (recFromDriver) {
        recDacBuf[recIdx] = (uint16_t)(2048 + x);
        recBuf[recIdx++] = adcAt32_lastSample();
        return x;
    }

    /* Each conversion is started one sample period before it is read */
    if ((ADC2->sts & ADC_CCE_FLAG) != 0) {
        recDacBuf[recIdx] = (uint16_t)(2048 + x);
        recBuf[recIdx++] = (uint16_t)ADC2->odt_bit.odt;
    }
    if (recIdx < recCount)
        ADC2->ctrl2_bit.ocswtrg = TRUE;

    return x;
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

void testsignal_init(void)
{
    adc_base_config_type cfg;

    for (size_t i = 0; i < TABLE_SIZE; i++)
        sineTable[i] =
            (int16_t)(32767.0f * sinf(2.0f * 3.14159265f * i / TABLE_SIZE));

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

    dacAt32_addHook(generatorHook);
}

int testsignal_record(uint8_t channel, uint32_t count, uint32_t div,
                      const uint16_t **samples, const uint16_t **dacSamples)
{
    if ((channel > 15) || (count == 0) || (count > TESTSIGNAL_REC_MAX)
        || (div == 0) || (div > SAMPLE_RATE / 1000))
        return -EINVAL;

    if (!recHooked) {
        if (dacAt32_addHook(recorderHook) < 0)
            return -ENOMEM;
        recHooked = true;
    }

    /* Stop the interrupt from writing before swapping the buffers */
    recCount = 0;
    free(recBuf);
    free(recDacBuf);
    recBuf = malloc(count * sizeof(uint16_t));
    recDacBuf = malloc(count * sizeof(uint16_t));
    if ((recBuf == NULL) || (recDacBuf == NULL)) {
        free(recBuf);
        free(recDacBuf);
        recBuf = NULL;
        recDacBuf = NULL;
        return -ENOMEM;
    }

    recFromDriver = (channel == DRIVER_CHANNEL);
    if (!recFromDriver) {
        analogPin(channel);
        /* Short enough to fit a 48 kHz sample period at any ADC clock */
        adc_ordinary_channel_set(ADC2, (adc_channel_select_type)channel, 1,
                                 ADC_SAMPLETIME_71_5);
        adc_ordinary_software_trigger_enable(ADC2, TRUE);
    }

    recDiv = div;
    recTick = 0;
    recIdx = 0;
    recCount = count;

    while (recIdx < recCount)
        k_msleep(10);

    *samples = recBuf;
    *dacSamples = recDacBuf;

    return 0;
}

int testsignal_start(enum testsignalShape newShape, uint32_t hz, uint32_t amp)
{
    if (amp > DAC_AT32_MAX)
        return -EINVAL;
    if ((newShape != TESTSIGNAL_OFF) && (hz >= (SAMPLE_RATE / 2)))
        return -EINVAL;

    shape = TESTSIGNAL_OFF;
    phase = 0;
    phaseStep = (uint32_t)(((uint64_t)hz << 32) / SAMPLE_RATE);
    amplitude = (int32_t)amp;
    shape = newShape;

    return 0;
}
