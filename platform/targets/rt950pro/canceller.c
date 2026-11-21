/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <zephyr/toolchain.h>
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

/*
 * Bench-measured defaults (2026-11-21, re-tools): A and B fitted to the node
 * response to white noise from the DAC (output error -24 dB), W designed
 * against receiver noise at the M17 receive gain, REG_48 0xB0A1 (node noise
 * -15 dB).
 */
float canceller_A[CANCELLER_NA] = {
    -0.7411686f,  -0.04175713f, -0.09729238f,
    -0.06038946f, -0.0738397f,  0.05445799f,
};
float canceller_B[CANCELLER_NB] = {
    -3.595676e-05f, -0.083524f,  0.009815273f, 0.02753548f,
    0.01695001f,    0.01428485f, 0.01330875f,  0.002427729f,
};
float canceller_W[CANCELLER_TAPS] = {
    12.40678f,    -0.9530387f, -2.918308f,   -2.676152f,   -1.753629f,
    -0.7907171f,  -0.2392252f, 0.4702322f,   1.04497f,     1.087806f,
    1.205316f,    0.5957534f,  0.1747287f,   -0.3464253f,  -0.4895755f,
    -0.6232552f,  -0.3814345f, -0.02886299f, -0.07446212f, -0.1370498f,
    0.04678791f,  0.2754506f,  0.6620709f,   0.7132238f,   0.6702173f,
    -0.09124659f, -0.3701508f, -0.574864f,   -0.4562318f,  -0.4480191f,
    -0.3116762f,  1.456502f,
};

/*
 * Histories, newest first from their position. Each value is stored twice,
 * CANCELLER_x apart, so that the window is always contiguous.
 */
struct history {
    size_t pos;
    float *buf;
    size_t len;
};

static float xBuf[2 * CANCELLER_NB];   /* x[n-1], x[n-2], ... */
static float yBuf[2 * CANCELLER_NA];   /* y[n-1], y[n-2], ... */
static float sBuf[2 * CANCELLER_TAPS]; /* s[n] - dc, s[n-1] - dc, ... */
static struct history xHist = { .pos = 0, .buf = xBuf, .len = CANCELLER_NB };
static struct history yHist = { .pos = 0, .buf = yBuf, .len = CANCELLER_NA };
static struct history sHist = { .pos = 0, .buf = sBuf, .len = CANCELLER_TAPS };
static float dc;
static volatile bool enabled;

/* Interrupt path: in RAM and without library calls, see dac_at32.c */
static inline __attribute__((always_inline)) void push(struct history *h,
                                                       float value)
{
    h->pos = (h->pos == 0) ? (h->len - 1) : (h->pos - 1);
    h->buf[h->pos] = value;
    h->buf[h->pos + h->len] = value;
}

static inline __attribute__((always_inline)) float dot(const float *coeff,
                                                       const struct history *h)
{
    const float *v = &h->buf[h->pos];
    float acc[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    size_t i = 0;

    /*
     * Four independent sums, so that each multiply-add does not wait for the
     * previous one: the compiler may not reorder float additions itself.
     */
    for (; (i + 4) <= h->len; i += 4) {
        acc[0] += coeff[i] * v[i];
        acc[1] += coeff[i + 1] * v[i + 1];
        acc[2] += coeff[i + 2] * v[i + 2];
        acc[3] += coeff[i + 3] * v[i + 3];
    }
    for (; i < h->len; i++)
        acc[0] += coeff[i] * v[i];

    return (acc[0] + acc[1]) + (acc[2] + acc[3]);
}

static inline __attribute__((always_inline)) int32_t roundToInt(float value)
{
    return (int32_t)(value + ((value >= 0.0f) ? 0.5f : -0.5f));
}

static AUDIO_ISR_CODE int32_t filter(uint16_t raw)
{
    float y = dot(canceller_B, &xHist) - dot(canceller_A, &yHist);
    push(&yHist, y);

    float s = (float)raw - y;
    dc += (s - dc) / DC_SHIFT;
    push(&sHist, s - dc);

    return roundToInt(s);
}

static AUDIO_ISR_CODE int32_t hook(int32_t sample)
{
    float x = (float)sample;

    if (enabled)
        x += dot(canceller_W, &sHist);

    if (x > DAC_AT32_MAX)
        x = DAC_AT32_MAX;
    if (x < -DAC_AT32_MAX)
        x = -DAC_AT32_MAX;

    int32_t out = roundToInt(x);
    push(&xHist, (float)out);

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
