/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <soc.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <drivers/audio/dac_at32.h>
#include "testsignal.h"

/*
 * Bench signals on DAC1 (PA4), through the per-sample hook of the DAC driver
 * (48 kHz): the test signal is added to the stream being played, if any.
 *
 * Canceller (TESTSIGNAL_CANCEL), a bench experiment: the DAC drives the
 * receive audio node (PC12 low) so that it cancels what the BK4829 chips put
 * there, read back on PA2 (ADC2 channel 2) every sample. Per sample n, with
 * x[n] the DAC output (stream plus correction):
 *   c[n] = sum_k W[k] s[n-1-k]                       correction
 *   y[n] = sum_j B[j] x[n-j] - sum_i A[i] y[n-1-i]   model of x on the node
 *   s[n] = v[n] - mean(v) - y[n]                     the BK4829 signal alone
 * A, B and W are loaded by the host.
 */

#define SAMPLE_RATE DAC_AT32_RATE
#define TABLE_BITS 8
#define TABLE_SIZE (1 << TABLE_BITS)

static int16_t sineTable[TABLE_SIZE];
static volatile uint32_t phase;
static volatile uint32_t phaseStep;
static volatile int32_t amplitude;
static volatile enum testsignalShape shape;
static uint32_t noiseState = 1;

float testsignal_cancelA[TESTSIGNAL_CANCEL_NA];
float testsignal_cancelB[TESTSIGNAL_CANCEL_NB];
float testsignal_cancelW[TESTSIGNAL_CANCEL_TAPS];
static float cancelX[TESTSIGNAL_CANCEL_NB];   /* x[n], x[n-1], ... */
static float cancelY[TESTSIGNAL_CANCEL_NA];   /* y[n-1], y[n-2], ... */
static float cancelS[TESTSIGNAL_CANCEL_TAPS]; /* s[n-1], s[n-2], ... */
static float cancelMean;
static uint16_t cancelV;
/* Time constant of the node DC estimate: 2^12 samples, about 85 ms */
#define CANCEL_MEAN_SHIFT 4096.0f
/* PA2: the receive audio node */
#define CANCEL_CHANNEL 2

/*
 * Recorder: one ADC2 conversion every recDiv samples. The buffers are
 * allocated by each recording and kept until the next one, for the host to
 * read them: the bench costs no memory until it is used.
 */
static uint16_t *recBuf;
static uint16_t *recDacBuf;
static uint32_t recDiv;
static volatile uint32_t recCount;
static volatile uint32_t recIdx;
static uint32_t recTick;

static void push(float *hist, size_t len, float value)
{
    memmove(&hist[1], &hist[0], (len - 1) * sizeof(float));
    hist[0] = value;
}

static int32_t clampDac(int32_t value)
{
    if (value > DAC_AT32_MAX)
        return DAC_AT32_MAX;
    if (value < -DAC_AT32_MAX)
        return -DAC_AT32_MAX;

    return value;
}

static int32_t generate(void)
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

static int32_t cancelCorrection(void)
{
    float c = 0.0f;

    for (size_t k = 0; k < TESTSIGNAL_CANCEL_TAPS; k++)
        c += testsignal_cancelW[k] * cancelS[k];

    return clampDac((int32_t)c);
}

static void cancelUpdate(int32_t x)
{
    float y = 0.0f;

    push(cancelX, TESTSIGNAL_CANCEL_NB, (float)x);
    for (size_t j = 0; j < TESTSIGNAL_CANCEL_NB; j++)
        y += testsignal_cancelB[j] * cancelX[j];
    for (size_t i = 0; i < TESTSIGNAL_CANCEL_NA; i++)
        y -= testsignal_cancelA[i] * cancelY[i];
    push(cancelY, TESTSIGNAL_CANCEL_NA, y);

    /* The conversion was started one sample period ago */
    if (adc_flag_get(ADC2, ADC_CCE_FLAG) == SET)
        cancelV = adc_ordinary_conversion_data_get(ADC2);
    adc_ordinary_software_trigger_enable(ADC2, TRUE);

    float v = (float)cancelV;
    cancelMean += (v - cancelMean) / CANCEL_MEAN_SHIFT;
    push(cancelS, TESTSIGNAL_CANCEL_TAPS, v - cancelMean - y);
}

static void record(int32_t x)
{
    if (recIdx >= recCount)
        return;
    if (++recTick < recDiv)
        return;

    recTick = 0;
    if (shape == TESTSIGNAL_CANCEL) {
        /* The canceller converts every sample: record its node reading */
        recDacBuf[recIdx] = (uint16_t)(2048 + x);
        recBuf[recIdx++] = cancelV;
        return;
    }

    /* Each conversion is started one sample period before it is read */
    if (adc_flag_get(ADC2, ADC_CCE_FLAG) == SET) {
        recDacBuf[recIdx] = (uint16_t)(2048 + x);
        recBuf[recIdx++] = adc_ordinary_conversion_data_get(ADC2);
    }
    if (recIdx < recCount)
        adc_ordinary_software_trigger_enable(ADC2, TRUE);
}

static int32_t hook(int32_t sample)
{
    int32_t x = sample + generate();

    if (shape == TESTSIGNAL_CANCEL) {
        x = clampDac(x + cancelCorrection());
        cancelUpdate(x);
    }

    x = clampDac(x);
    record(x);

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

static void selectChannel(uint8_t channel)
{
    analogPin(channel);
    /* Short enough to fit a 48 kHz sample period at any ADC clock */
    adc_ordinary_channel_set(ADC2, (adc_channel_select_type)channel, 1,
                             ADC_SAMPLETIME_71_5);
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

    dacAt32_setHook(hook);
}

int testsignal_record(uint8_t channel, uint32_t count, uint32_t div,
                      const uint16_t **samples, const uint16_t **dacSamples)
{
    if ((channel > 15) || (count == 0) || (count > TESTSIGNAL_REC_MAX)
        || (div == 0) || (div > SAMPLE_RATE / 1000))
        return -EINVAL;
    if ((shape == TESTSIGNAL_CANCEL) && (channel != CANCEL_CHANNEL))
        return -EBUSY;

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

    selectChannel(channel);

    recDiv = div;
    recTick = 0;
    recIdx = 0;
    adc_ordinary_software_trigger_enable(ADC2, TRUE);
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

    if (newShape == TESTSIGNAL_CANCEL) {
        selectChannel(CANCEL_CHANNEL);
        memset(cancelX, 0, sizeof(cancelX));
        memset(cancelY, 0, sizeof(cancelY));
        memset(cancelS, 0, sizeof(cancelS));
        cancelMean = 0.0f;
        cancelV = 0;
        adc_ordinary_software_trigger_enable(ADC2, TRUE);
    }

    phase = 0;
    phaseStep = (uint32_t)(((uint64_t)hz << 32) / SAMPLE_RATE);
    amplitude = (int32_t)amp;
    shape = newShape;

    return 0;
}
