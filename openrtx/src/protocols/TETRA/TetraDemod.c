/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <math.h>
#include <string.h>
#include "protocols/TETRA/TetraDemod.h"

#define PI_F 3.14159265f
#define SYMBOL_RATE 18000.0f
#define SPS TETRA_DEMOD_SPS
#define HIST_MASK (TETRA_DEMOD_HIST - 1)
#define EQ_HALF (TETRA_DEMOD_EQ_TAPS / 2)

#define WARMUP_SYMBOLS 1024  /* Gain and offset from moments */
#define ACQUIRE_SYMBOLS 1024 /* Then the timing cost over a full symbol */
#ifndef COST_ALPHA
#define COST_ALPHA (1.0f / 256)
#endif
/*
 * Timing loop, proportional and integral: the cost average lags by about
 * 1 / COST_ALPHA symbols, so keep the loop several times slower than that.
 */
#ifndef TIMING_KP
#define TIMING_KP 0.001f
#endif
#ifndef TIMING_KI
#define TIMING_KI 0.0000005f
#endif
#ifndef FIT_ALPHA
#define FIT_ALPHA (1.0f / 2048)
#endif
#ifndef EQ_MU
#define EQ_MU 0.002f
#endif
#define ERR_ALPHA (1.0f / 1024)
#define ERR_FAST_ALPHA (1.0f / 64)
#define SOFT_SCALE 40.0f /* Soft value at pi/4 from the decision boundary */
/*
 * Losing the signal: random phases give an RMS error of 58% of pi/4, a
 * clean signal 10 to 20%. Above BAD_ERROR the loops stop adapting, and after
 * BAD_SYMBOLS (0.45 s) of it the demodulator acquires again from scratch.
 */
#define BAD_ERROR (0.45f * PI_F / 4)
#define BAD_SYMBOLS 8192
/* After acquiring, the loops converge freely this long (0.45 s) */
#define GRACE_SYMBOLS (WARMUP_SYMBOLS + ACQUIRE_SYMBOLS + 8192)
/* A timing jump degrades the signal: only look for one above this error */
#define SLIP_ERROR (0.30f * PI_F / 4)
/* Follow a timing jump of 2 or more samples when its cost is this lower */
#define SLIP_RATIO 0.5f
#define POLARITY_WINDOW 16384

/* Synchronisation training sequence y, 38 bits, first bit in bit 37 */
static const uint64_t SYNC_SEQ = 0x30673A7067ULL;
#define SYNC_MASK 0x3FFFFFFFFFULL

static float wrapf(float phi)
{
    while (phi >= PI_F)
        phi -= 2 * PI_F;
    while (phi < -PI_F)
        phi += 2 * PI_F;
    return phi;
}

/**
 * Nearest constellation point, in units of pi/4: -3, -1, 1 or 3.
 */
static int decide(float phi)
{
    phi = wrapf(phi);
    if (phi >= PI_F / 2)
        return 3;
    if (phi >= 0)
        return 1;
    if (phi >= -PI_F / 2)
        return -1;
    return -3;
}

/**
 * Wrapped decision error, in radians.
 */
static float decisionError(float phi)
{
    return wrapf(phi - decide(phi) * (PI_F / 4));
}

void tetraDemod_init(struct tetraDemod *dm)
{
    memset(dm, 0, sizeof(*dm));

    /*
     * 48 -> 144 kHz interpolator: windowed sinc, cut-off 21 kHz, split in
     * three branches, each normalised to unity DC gain.
     */
    const int len = 3 * TETRA_DEMOD_INTERP;
    const float fc = 21000.0f / (3 * TETRA_DEMOD_IN_RATE);
    for (int p = 0; p < 3; p++) {
        float total = 0;
        for (int j = 0; j < TETRA_DEMOD_INTERP; j++) {
            int n = 3 * j + p;
            float t = n - (len - 1) / 2.0f;
            float s = (t == 0) ? 2 * fc : sinf(2 * PI_F * fc * t) / (PI_F * t);
            float w = 0.42f - 0.5f * cosf(2 * PI_F * n / (len - 1))
                    + 0.08f * cosf(4 * PI_F * n / (len - 1));
            dm->fir[p][j] = s * w;
            total += s * w;
        }
        for (int j = 0; j < TETRA_DEMOD_INTERP; j++)
            dm->fir[p][j] /= total;
    }

    dm->nextInt = TETRA_DEMOD_HIST / 2;
    dm->gain = 1.0f;
    dm->eq[EQ_HALF] = 1.0f;
    dm->eqInit[EQ_HALF] = 1.0f;
}

/**
 * Acquire the timing phase again and restart the equalizer, keeping the
 * gain and offset fit, the clock offset and the polarity. Symbols keep
 * coming out meanwhile, so the receiver's bit count stays continuous.
 */
static void reacquire(struct tetraDemod *dm)
{
    dm->symbols = WARMUP_SYMBOLS + TETRA_DEMOD_EQ_TAPS;
    dm->acquired = false;
    memset(dm->cost, 0, sizeof(dm->cost));
    memcpy(dm->eq, dm->eqInit, sizeof(dm->eq));
    dm->eqBias = 0;
    dm->errPow = 0;
    dm->errFast = 0;
    dm->badSymbols = 0;
    dm->reacquired++;
}

/**
 * Linear interpolation of the integrated signal at the next symbol position
 * plus an offset.
 */
static float sampleAt(const struct tetraDemod *dm, int offset)
{
    uint32_t i = dm->nextInt + offset;
    float f = dm->nextFrac;
    return dm->hist[i & HIST_MASK] * (1 - f)
         + dm->hist[(i + 1) & HIST_MASK] * f;
}

/**
 * Move the next symbol position.
 */
static void advance(struct tetraDemod *dm, float delta)
{
    float f = dm->nextFrac + delta;
    int whole = (int)floorf(f);
    dm->nextInt += whole;
    dm->nextFrac = f - whole;
}

/**
 * Update the polarity detector with one symbol (in units of pi/4, before
 * any inversion).
 */
static void trackPolarity(struct tetraDemod *dm, int q)
{
    /* TETRA mapping: b1 = 1 for a negative phase change, b2 = 1 for |3| */
    unsigned b1 = q < 0;
    unsigned b2 = (q == 3) || (q == -3);
    dm->srNorm = (dm->srNorm << 2) | (b1 << 1) | b2;
    dm->srInv = (dm->srInv << 2) | ((b1 ^ 1) << 1) | b2;
    /* The sequence can start on either bit of a dibit */
    for (int s = 0; s < 2; s++) {
        if (((dm->srNorm >> s) & SYNC_MASK) == SYNC_SEQ)
            dm->hitsNorm++;
        if (((dm->srInv >> s) & SYNC_MASK) == SYNC_SEQ)
            dm->hitsInv++;
    }
    if (++dm->polaritySymbols == POLARITY_WINDOW) {
        bool inv = dm->inverted;
        if (!inv && dm->hitsInv > dm->hitsNorm + 2)
            dm->inverted = true;
        else if (inv && dm->hitsNorm > dm->hitsInv + 2)
            dm->inverted = false;
        dm->hitsNorm = dm->hitsInv = dm->polaritySymbols = 0;
    }
}

/**
 * Handle one integrate-and-dump value: fit, equalize, decide.
 *
 * @return number of soft bits written (0 or 2).
 */
static size_t symbol(struct tetraDemod *dm, float d, int8_t *soft)
{
    dm->symbols++;

    if (dm->symbols <= WARMUP_SYMBOLS) {
        /* Moments: mean, mean absolute deviation and variance */
        float a = 1.0f / dm->symbols;
        dm->sx += a * (d - dm->sx);
        float dev = d - dm->sx;
        dm->st += a * (fabsf(dev) - dm->st);
        dm->sxx += a * (dev * dev - dm->sxx);
        if (dm->symbols == WARMUP_SYMBOLS) {
            float v = dm->sxx;
            dm->offset = dm->sx;
            dm->gain = (PI_F / 2) / dm->st;
            /* Seed the least-squares moments: t = gain * (d - offset) */
            dm->sxx = dm->sx * dm->sx + v;
            dm->st = 0;
            dm->sxt = dm->gain * v;
        }
        return 0;
    }

    /*
     * Adapt only on a usable signal, not during a fade or a glitch: the
     * fast error average reacts within a few milliseconds.
     */
    bool converging = dm->symbols < GRACE_SYMBOLS;
    bool adapt = converging || dm->errFast < BAD_ERROR * BAD_ERROR;

    /* Least-squares fit of t = gain * (d - offset) to the decisions */
    float x = dm->gain * (d - dm->offset);
    float t = decide(x) * (PI_F / 4);
    t += 2 * PI_F * roundf((x - t) / (2 * PI_F)); /* Keep 2 pi clicks */
    if (adapt) {
        dm->sx += FIT_ALPHA * (d - dm->sx);
        dm->sxx += FIT_ALPHA * (d * d - dm->sxx);
        dm->st += FIT_ALPHA * (t - dm->st);
        dm->sxt += FIT_ALPHA * (d * t - dm->sxt);
    }
    float var = dm->sxx - dm->sx * dm->sx;
    if (adapt && var > 0) {
        float g = (dm->sxt - dm->sx * dm->st) / var;
        if (g > 0) {
            dm->gain = g;
            dm->offset = dm->sx - dm->st / g;
        }
    }

    /* Equalizer: the decision is for the symbol EQ_HALF symbols ago */
    memmove(&dm->eqIn[0], &dm->eqIn[1], sizeof(dm->eqIn) - sizeof(float));
    dm->eqIn[TETRA_DEMOD_EQ_TAPS - 1] = x;
    if (dm->symbols < WARMUP_SYMBOLS + TETRA_DEMOD_EQ_TAPS)
        return 0;
    float y = dm->eqBias;
    float pow = 1e-3f;
    for (int i = 0; i < TETRA_DEMOD_EQ_TAPS; i++) {
        y += dm->eq[i] * dm->eqIn[i];
        pow += dm->eqIn[i] * dm->eqIn[i];
    }
    float e = decisionError(y);
    if (dm->acquired && adapt) {
        float mu = EQ_MU * e / pow;
        for (int i = 0; i < TETRA_DEMOD_EQ_TAPS; i++)
            dm->eq[i] -= mu * dm->eqIn[i];
        dm->eqBias -= EQ_MU * e;
    }
    dm->errPow += ERR_ALPHA * (e * e - dm->errPow);
    dm->errFast += ERR_FAST_ALPHA * (e * e - dm->errFast);

    /* Lost the signal for long: start again */
    if (dm->acquired && !converging && dm->errPow >= BAD_ERROR * BAD_ERROR) {
        if (++dm->badSymbols >= BAD_SYMBOLS) {
            reacquire(dm);
            return 0;
        }
    } else {
        dm->badSymbols = 0;
    }

    y = wrapf(y);
    trackPolarity(dm, decide(y));
    if (dm->inverted)
        y = -y;

    /* Soft bits: b1 = 1 for a negative phase change, b2 for |phi| > pi/2 */
    float k = SOFT_SCALE / (PI_F / 4);
    float s1 = -y * k;
    float s2 = (fabsf(y) - PI_F / 2) * k;
    s1 = s1 > 127 ? 127 : (s1 < -127 ? -127 : s1);
    s2 = s2 > 127 ? 127 : (s2 < -127 ? -127 : s2);
    soft[0] = (int8_t)lrintf(s1);
    soft[1] = (int8_t)lrintf(s2);
    return 2;
}

/**
 * Timing: running decision error at the 8 phases around the symbol, then
 * steer to the minimum (parabolic interpolation, circular).
 */
static void timing(struct tetraDemod *dm)
{
    if (dm->symbols <= WARMUP_SYMBOLS)
        return;

    for (int o = 0; o < SPS; o++) {
        float v = sampleAt(dm, o - SPS / 2);
        float e = decisionError(dm->gain * (v - dm->offset));
        dm->cost[o] += COST_ALPHA * (e * e - dm->cost[o]);
    }

    int best = 0;
    for (int o = 1; o < SPS; o++) {
        if (dm->cost[o] < dm->cost[best])
            best = o;
    }
    float cm = dm->cost[(best + SPS - 1) % SPS];
    float c0 = dm->cost[best];
    float cp = dm->cost[(best + 1) % SPS];
    float den = cm - 2 * c0 + cp;
    float vertex = best - SPS / 2 + (den > 0 ? 0.5f * (cm - cp) / den : 0);

    if (!dm->acquired) {
        if (dm->symbols < WARMUP_SYMBOLS + ACQUIRE_SYMBOLS)
            return;
        /* Jump to the best phase once, then track */
        advance(dm, vertex);
        dm->drift += vertex;
        dm->acquired = true;
        memset(dm->cost, 0, sizeof(dm->cost));
        return;
    }
    /*
     * The signal's timing jumped (lost samples, a replayed recording): follow
     * at once, moving the costs along with the position.
     */
    int jump = best - SPS / 2;
    if ((jump >= 2 || jump <= -2) && dm->symbols >= GRACE_SYMBOLS
        && dm->errPow > SLIP_ERROR * SLIP_ERROR
        && dm->cost[best] < SLIP_RATIO * dm->cost[SPS / 2]) {
        float moved[SPS];
        for (int o = 0; o < SPS; o++)
            moved[o] = dm->cost[(o + jump + SPS) % SPS];
        memcpy(dm->cost, moved, sizeof(moved));
        advance(dm, (float)jump);
        dm->drift += jump;
        dm->slips++;
        return;
    }

    dm->rate += TIMING_KI * vertex;
    float step = TIMING_KP * vertex + dm->rate;
    advance(dm, step);
    dm->drift += step;
}

size_t tetraDemod_process(struct tetraDemod *dm, const int16_t *in, size_t n,
                          int8_t *soft)
{
    size_t out = 0;

    for (size_t k = 0; k < n; k++) {
        dm->in[dm->inPos] = in[k];
        for (int p = 0; p < 3; p++) {
            /* Interpolate one 144 kHz sample */
            float acc = 0;
            unsigned idx = dm->inPos;
            for (int j = 0; j < TETRA_DEMOD_INTERP; j++) {
                acc += dm->fir[p][j] * dm->in[idx];
                idx = idx ? idx - 1 : TETRA_DEMOD_INTERP - 1;
            }
            /* Integrate over one symbol */
            dm->sum[dm->sumPos] = acc;
            dm->sumPos = (dm->sumPos + 1) % SPS;
            float d = 0;
            for (int i = 0; i < SPS; i++)
                d += dm->sum[i];
            dm->hist[dm->written & HIST_MASK] = d;
            dm->written++;

            /* Symbols whose window (up to +SPS/2 + 1) is complete */
            while ((int32_t)(dm->written - dm->nextInt) >= SPS / 2 + 2) {
                timing(dm);
                float v = sampleAt(dm, 0);
                out += symbol(dm, v, soft + out);
                dm->nextInt += SPS;
            }
        }
        dm->inPos = (dm->inPos + 1) % TETRA_DEMOD_INTERP;
    }
    return out;
}

unsigned tetraDemod_phaseError(const struct tetraDemod *dm)
{
    return (unsigned)lrintf(100.0f * sqrtf(dm->errPow) / (PI_F / 4));
}

int tetraDemod_carrierOffset(const struct tetraDemod *dm)
{
    return (int)lrintf(dm->gain * dm->offset * SYMBOL_RATE / (2 * PI_F));
}

int tetraDemod_clockOffset(const struct tetraDemod *dm)
{
    return (int)lrintf(dm->rate / SPS * 1e6f);
}

void tetraDemod_setEqualizer(struct tetraDemod *dm, const float *taps)
{
    memcpy(dm->eq, taps, sizeof(dm->eq));
    memcpy(dm->eqInit, taps, sizeof(dm->eqInit));
}

uint32_t tetraDemod_reacquisitions(const struct tetraDemod *dm)
{
    return dm->reacquired + dm->slips;
}
