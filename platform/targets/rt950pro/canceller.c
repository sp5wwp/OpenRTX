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

/* Bound of the model output, far beyond what the DAC can put on the node */
#define MODEL_LIMIT 65536.0f

/* Time constant of the DC estimate of s: 2^12 samples, about 85 ms */
#define DC_SHIFT 4096.0f

/*
 * Bench-measured defaults (2026-11-21, re-tools): A and B fitted to the node
 * response to white noise from the DAC (output error -23 dB), W designed
 * against receiver noise at the M17 receive gain, REG_48 0xB0A1 (node noise
 * -14 dB). Both depend on the timing of the DAC interrupt (dac_at32.c): measure
 * them again after changing it.
 */
float canceller_A[CANCELLER_NA] = {
    -2.655914f, 2.514668f, -1.142548f, 0.3952799f, -0.1295428f, 0.01816994f,
};
float canceller_B[CANCELLER_NB] = {
    -0.0001303004f, -0.09520151f,  0.2101778f,   -0.1375548f,
    0.0299244f,     -0.009969073f, 0.001960546f, 0.0007914269f,
};
float canceller_W[CANCELLER_TAPS] = {
    11.70068f,   -0.3121707f, -2.696583f,  -2.689268f,   -2.021695f,
    -0.8237088f, 0.09395762f, 0.612531f,   1.01026f,     0.8198051f,
    1.042926f,   0.6431663f,  0.3165381f,  -0.4427603f,  -0.7419629f,
    -0.5339395f, -0.1457942f, 0.1453318f,  -0.08167463f, -0.08432216f,
    -0.1434417f, 0.1606878f,  0.8160624f,  0.515118f,    0.4351506f,
    0.05934558f, -0.1136553f, -0.4595875f, -0.6254457f,  -0.4171143f,
    -0.3288724f, 1.440938f,
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

/* Restart from silence, after a model output out of range */
static AUDIO_ISR_CODE void reset(float level)
{
    for (size_t i = 0; i < 2 * CANCELLER_NA; i++)
        yBuf[i] = 0.0f;
    for (size_t i = 0; i < 2 * CANCELLER_TAPS; i++)
        sBuf[i] = 0.0f;
    dc = level;
}

static AUDIO_ISR_CODE int32_t filter(uint16_t raw)
{
    float y = dot(canceller_B, &xHist) - dot(canceller_A, &yHist);

    /*
     * The model is stable, but a NaN or an overflow (e.g. coefficients being
     * replaced while it runs) would otherwise stay in its state for good: the
     * comparisons are false for a NaN too.
     */
    if (!((y < MODEL_LIMIT) && (y > -MODEL_LIMIT))) {
        reset((float)raw);
        y = 0.0f;
    }
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
