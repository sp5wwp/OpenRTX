/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <math.h>
#include <zephyr/kernel.h>
#include <drivers/audio/adc_at32.h>
#include <drivers/audio/dac_at32.h>
#include "clockcal.h"

/*
 * The ADC samples, taken at each DAC driver tick, are mixed down by the
 * nominal tone frequency (complex oscillator) and summed over windows: the
 * phase of the windows moves by 2 pi (f_true - f_nominal) T per window, where
 * f_nominal assumes an exact MCU clock. A linear fit of the window phases
 * gives the frequency seen on the MCU clock, and so its error.
 */

#define WINDOW_TICKS 4800 /* 0.1 s */
#define WINDOWS 6
#define SETTLE_TICKS 4800 /* the tone and the node settle */
/* Largest plausible MCU clock error, 0.1 ppm units */
#define MAX_ERROR 20000

static volatile bool running;
static uint32_t tick;
static float cosStep, sinStep; /* oscillator rotation per tick */
static float oscRe, oscIm;
static float accRe, accIm;
static float dc;
static volatile uint32_t window;
static float winRe[WINDOWS], winIm[WINDOWS];
static K_SEM_DEFINE(doneSem, 0, 1);
static int doneDeferred;
static int32_t lastError;

static void doneEvent(void)
{
    k_sem_give(&doneSem);
}

/* DAC hook used for its timing only: the output sample is not changed */
static AUDIO_ISR_CODE int32_t hook(int32_t sample)
{
    if (!running)
        return sample;

    float v = (float)adcAt32_lastSample();
    dc += (v - dc) * (1.0f / 1024.0f);

    if (++tick <= SETTLE_TICKS)
        return sample;

    v -= dc;
    accRe += v * oscRe;
    accIm -= v * oscIm;

    float re = (oscRe * cosStep) - (oscIm * sinStep);
    oscIm = (oscRe * sinStep) + (oscIm * cosStep);
    oscRe = re;

    if (((tick - SETTLE_TICKS) % WINDOW_TICKS) != 0)
        return sample;

    /* Keep the oscillator on the unit circle */
    float mag = (oscRe * oscRe) + (oscIm * oscIm);
    float k = 1.5f - (0.5f * mag);
    oscRe *= k;
    oscIm *= k;

    winRe[window] = accRe;
    winIm[window] = accIm;
    accRe = 0.0f;
    accIm = 0.0f;

    if (++window >= WINDOWS) {
        running = false;
        dacAt32_defer(doneDeferred);
    }

    return sample;
}

void clockcal_init(void)
{
    doneDeferred = dacAt32_addDeferred(doneEvent);
    dacAt32_addHook(hook);
}

int clockcal_measure(float toneHz, int32_t *error)
{
    float w = 2.0f * 3.14159265f * toneHz / (float)DAC_AT32_RATE;

    unsigned int key = dacAt32_lock();
    cosStep = cosf(w);
    sinStep = sinf(w);
    oscRe = 1.0f;
    oscIm = 0.0f;
    accRe = 0.0f;
    accIm = 0.0f;
    dc = (float)adcAt32_lastSample();
    tick = 0;
    window = 0;
    k_sem_reset(&doneSem);
    running = true;
    dacAt32_unlock(key);

    if (k_sem_take(&doneSem, K_MSEC(3000)) != 0) {
        running = false;
        return -EIO;
    }

    /* Phase of each window, unwrapped, then a least squares slope */
    float phase[WINDOWS];
    float amp = 0.0f;
    for (int i = 0; i < WINDOWS; i++) {
        phase[i] = atan2f(winIm[i], winRe[i]);
        if (i > 0) {
            while ((phase[i] - phase[i - 1]) > 3.14159265f)
                phase[i] -= 2.0f * 3.14159265f;
            while ((phase[i] - phase[i - 1]) < -3.14159265f)
                phase[i] += 2.0f * 3.14159265f;
        }
        amp += sqrtf((winRe[i] * winRe[i]) + (winIm[i] * winIm[i]));
    }

    float sx = 0.0f, sy = 0.0f, sxx = 0.0f, sxy = 0.0f;
    for (int i = 0; i < WINDOWS; i++) {
        sx += (float)i;
        sy += phase[i];
        sxx += (float)(i * i);
        sxy += (float)i * phase[i];
    }
    float slope = ((WINDOWS * sxy) - (sx * sy))
                / ((WINDOWS * sxx) - (sx * sx)); /* rad per window */

    float residual = 0.0f;
    float b = (sy - (slope * sx)) / WINDOWS;
    for (int i = 0; i < WINDOWS; i++) {
        float e = phase[i] - (b + (slope * (float)i));
        residual += e * e;
    }

    /* A weak or unsteady tone: no result */
    if ((amp < (WINDOWS * WINDOW_TICKS * 2.0f))
        || (residual > (WINDOWS * 0.05f)))
        return -EIO;

    /*
     * Frequency offset seen on the MCU clock, Hz. A slow MCU clock counts
     * fewer ticks per second: the tone looks higher.
     */
    float offset = slope * (float)DAC_AT32_RATE
                 / (2.0f * 3.14159265f * WINDOW_TICKS);
    float ratio = toneHz / (toneHz + offset);

    /*
     * The ticks already run at the rate corrected for the trim in effect:
     * the MCU clock error is the trim plus what is left of it.
     */
    int32_t e = (int32_t)lroundf((ratio - 1.0f) * 1e7f)
              + dacAt32_getClockTrim();

    /* Beyond any crystal or resonator in use: not the expected tone */
    if ((e > MAX_ERROR) || (e < -MAX_ERROR))
        return -EIO;

    lastError = e;
    *error = lastError;

    return 0;
}

int32_t clockcal_lastError(void)
{
    return lastError;
}
