/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <math.h>
#include <string.h>
#include <drivers/audio/adc_at32.h>
#include <drivers/audio/dac_at32.h>
#include "canceller.h"

/*
 * Receive audio node processing. Both BK4829 AF outputs and DAC1 (PC12 low)
 * drive the same node, which feeds the speaker amplifier and is read on PA2
 * by the ADC. Per sample n, at the DAC driver rate:
 *
 * - ADC filter: the DAC contribution to the node, y, is modelled from the past
 *   DAC outputs x and removed from the ADC sample v, leaving the BK4829
 *   signal s alone for the demodulator:
 *     y[n] = sum_j B[j] x[n-1-j] - sum_i A[i] y[n-1-i]
 *     s[n] = v[n] - y[n]
 * - DAC hook: the correction c, predicted from s, is added to the stream
 *   sample so that the BK4829 signal is cancelled on the node, and so at the
 *   speaker, under the decoded voice:
 *     c[n] = sum_k W[k] (s[n-k] - mean(s))
 *     x[n] = voice[n] + c[n]
 *
 * The model is identified on the bench by injecting noise from the DAC, and W
 * is designed from it and from recorded receiver baseband (the re-tools).
 */

/* Time constant of the DC estimate of s: 2^12 samples, about 85 ms */
#define DC_SHIFT 4096.0f

float canceller_A[CANCELLER_NA];
float canceller_B[CANCELLER_NB];
float canceller_W[CANCELLER_TAPS];

static float xHist[CANCELLER_NB];   /* x[n-1], x[n-2], ... */
static float yHist[CANCELLER_NA];   /* y[n-1], y[n-2], ... */
static float sHist[CANCELLER_TAPS]; /* s[n] - dc, s[n-1] - dc, ... */
static float dc;
static volatile bool enabled;

static void push(float *hist, size_t len, float value)
{
    memmove(&hist[1], &hist[0], (len - 1) * sizeof(float));
    hist[0] = value;
}

static int32_t filter(uint16_t raw)
{
    float y = 0.0f;

    for (size_t j = 0; j < CANCELLER_NB; j++)
        y += canceller_B[j] * xHist[j];
    for (size_t i = 0; i < CANCELLER_NA; i++)
        y -= canceller_A[i] * yHist[i];
    push(yHist, CANCELLER_NA, y);

    float s = (float)raw - y;
    dc += (s - dc) / DC_SHIFT;
    push(sHist, CANCELLER_TAPS, s - dc);

    return (int32_t)lrintf(s);
}

static int32_t clampDac(float value)
{
    if (value > DAC_AT32_MAX)
        return DAC_AT32_MAX;
    if (value < -DAC_AT32_MAX)
        return -DAC_AT32_MAX;

    return (int32_t)lrintf(value);
}

static int32_t hook(int32_t sample)
{
    float x = (float)sample;

    if (enabled) {
        for (size_t k = 0; k < CANCELLER_TAPS; k++)
            x += canceller_W[k] * sHist[k];
    }

    int32_t out = clampDac(x);
    push(xHist, CANCELLER_NB, (float)out);

    return out;
}

void canceller_init()
{
    dc = (float)adcAt32_lastSample();
    adcAt32_setFilter(filter);
    dacAt32_addHook(hook);
}

void canceller_enable(bool enable)
{
    enabled = enable;
}
