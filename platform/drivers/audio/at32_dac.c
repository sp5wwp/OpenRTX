/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "peripherals/gpio.h"
#include "drivers/GPIO/gpio_zephyr.h"
#include "at32_dac.h"

// AT32F403A registers: DAC1 on PA4, clocked by TMR6 and fed by DMA2 channel 3
#define CRM_AHBEN 0x40021014
#define CRM_APB1EN 0x4002101C
#define AHB_DMA2 BIT(1)
#define APB1_TMR6 BIT(4)
#define APB1_DAC BIT(29)
#define DAC_CTRL 0x40007400
#define DAC_DHR12R1 0x40007408
#define DAC_CTRL_DMA1 (BIT(0) | BIT(2) | BIT(12)) // EN1, TEN1 (TMR6), DMAEN1
// DAC1's half of the register: DAC2 is the PA drive (radio_rt950pro.cpp)
#define DAC_CTRL_CH1_MASK 0x0000FFFF
#define TMR6_CTRL1 0x40001000
#define TMR6_CTRL2 0x40001004
#define TMR6_SWEVT 0x40001014
#define TMR6_DIV 0x40001028
#define TMR6_PR 0x4000102C
#define TMR6_TRGO_OVF (2 << 4)
#define DMA2_STS 0x40020400
#define DMA2_CLR 0x40020404
#define DMA2_C3_CTRL 0x40020430
#define DMA2_C3_DTCNT 0x40020434
#define DMA2_C3_PADDR 0x40020438
#define DMA2_C3_MADDR 0x4002043C
#define DMA_C3_FDT BIT(9)
#define DMA_C3_HDT BIT(10)
#define DMA_C3_ALL (0xF << 8)
// Enabled, full and half transfer interrupts, memory to peripheral,
// circular, memory increment, 16 bit each way, high priority
#define DMA_CTRL_DAC                                                \
    (BIT(0) | BIT(1) | BIT(2) | BIT(4) | BIT(5) | BIT(7) | (1 << 8) \
     | (1 << 10) | (2 << 12))

#define TIMER_HZ 120000000 // APB1 at 60MHz, its timers at twice that
#define SPEECH_HZ 8000
#define DAC_HZ 48000
#define UP (DAC_HZ / SPEECH_HZ)
#define DAC_MID 2048
// Speech to DAC1 counts: SPEECH_GAIN times full scale, with a limiter holding
// its peaks under LIMIT_PEAK. DAC1 reaches the speaker far weaker than a BK4829
// does, and beside the receiver's IF on the same net the speech needs all the
// level it can have; clipped, its harmonics land in the IF's band
#define SPEECH_UNITY (1.0f / 16.0f) // A stream sample in DAC1 counts
#define SPEECH_GAIN 4.0f
#define LIMIT_PEAK 1900.0f          // Of DAC1's 2047 either way
#define LIMIT_LOOK 32               // Samples the limiter sees ahead: 4ms
#define LIMIT_RELEASE (SPEECH_GAIN / 400.0f) // Gain back per sample: 50ms
#define PHASE_TAPS 8
#define SPEECH_LP_TAPS 43

// Speech low-pass at the stream's 8kHz: flat to 2.7kHz and 60dB down from
// 3.4kHz (Kaiser 5.65). The receiver's IF band starts at 3.5kHz on the same
// net, and what speech has from 3kHz up costs it frames. Half of the
// symmetric filter, its centre last
static const float speechLp[SPEECH_LP_TAPS / 2 + 1] = {
    0.000012f,  -0.000434f, 0.001047f,  -0.001246f, 0.000285f,  0.002017f,
    -0.004619f, 0.005377f,  -0.002247f, -0.004854f, 0.012794f,  -0.015918f,
    0.009057f,  0.008369f,  -0.029626f, 0.041688f,  -0.030532f, -0.011336f,
    0.079068f,  -0.155028f, 0.214839f,  0.762578f,
};

// 3kHz low-pass at 48kHz in six phases, Kaiser 6: 78dB down from 5kHz, where
// the receiver's low-pass starts to let through what the mixer puts there
static const float interpolate[UP][PHASE_TAPS] = {
    { 0.000236f, 0.010240f, -0.081339f, 0.248254f, 0.744858f, 0.132011f,
      -0.065313f, 0.011165f },
    { 0.001335f, 0.004812f, -0.085302f, 0.376491f, 0.700078f, 0.037371f,
      -0.044060f, 0.009293f },
    { 0.003409f, -0.006309f, -0.070208f, 0.503908f, 0.616262f, -0.030324f,
      -0.023161f, 0.006291f },
    { 0.006291f, -0.023161f, -0.030324f, 0.616262f, 0.503908f, -0.070208f,
      -0.006309f, 0.003409f },
    { 0.009293f, -0.044060f, 0.037371f, 0.700078f, 0.376491f, -0.085302f,
      0.004812f, 0.001335f },
    { 0.011165f, -0.065313f, 0.132011f, 0.744858f, 0.248254f, -0.081339f,
      0.010240f, 0.000236f },
};

static const struct gpioPin pa4 = { &GpioA, 4 };

static struct {
    struct streamCtx *ctx;     ///< Running stream, NULL when none
    uint16_t *out;             ///< DAC1's samples for the stream, on the heap
    struct k_sem sync;         ///< Given at each half of the stream
    float history[PHASE_TAPS]; ///< Last speech samples, newest first
    float lpHistory[SPEECH_LP_TAPS]; ///< Last stream samples, newest first
    float delayed[LIMIT_LOOK];       ///< Speech the limiter has yet to let out
    float needed[LIMIT_LOOK];        ///< Gain each of those samples allows
    float eased[LIMIT_LOOK];         ///< Least gain needed, released slowly
    float easedSum;                  ///< Sum of eased
    float held;                      ///< Last of eased
    uint8_t limitPos;         ///< Place of the newest sample in the three
    volatile bool stopping;   ///< Stop at the stream's next half
    volatile uint8_t playing; ///< Half DAC1 is playing
    uint8_t idle;             ///< Half last given to the writer
} dac;

/**
 * Stop DAC1, its DMA and TMR6; DAC1's pin goes high impedance.
 */
static void halt(void)
{
    sys_write32(0, TMR6_CTRL1);
    sys_write32(0, DMA2_C3_CTRL);
    sys_write32(DMA_C3_ALL, DMA2_CLR);
    sys_clear_bits(DAC_CTRL, DAC_CTRL_CH1_MASK);
}

/**
 * Low-pass a stream sample with speechLp.
 *
 * @param sample: stream sample.
 * @return the filter's output.
 */
__attribute__((optimize("O3"))) static float lowPass(const float sample)
{
    memmove(&dac.lpHistory[1], &dac.lpHistory[0],
            (SPEECH_LP_TAPS - 1) * sizeof(float));
    dac.lpHistory[0] = sample;

    float acc = speechLp[SPEECH_LP_TAPS / 2]
              * dac.lpHistory[SPEECH_LP_TAPS / 2];
    for (size_t k = 0; k < SPEECH_LP_TAPS / 2; k++)
        acc += speechLp[k]
             * (dac.lpHistory[k] + dac.lpHistory[SPEECH_LP_TAPS - 1 - k]);

    return acc;
}

/**
 * Reset the limiter: full gain, nothing waiting.
 */
static void limitReset(void)
{
    memset(dac.delayed, 0, sizeof(dac.delayed));
    for (size_t i = 0; i < LIMIT_LOOK; i++) {
        dac.needed[i] = SPEECH_GAIN;
        dac.eased[i] = SPEECH_GAIN;
    }

    dac.easedSum = SPEECH_GAIN * LIMIT_LOOK;
    dac.held = SPEECH_GAIN;
    dac.limitPos = 0;
}

/**
 * Give a speech sample its gain, less whatever keeps the peaks around it
 * under LIMIT_PEAK. The gain is the least one the samples LIMIT_LOOK ahead
 * allow, eased down over as many samples and released slowly, so it is down
 * by the time a peak comes out and never steps.
 *
 * @param sample: speech sample, in DAC1 counts at a gain of one.
 * @return the sample that came LIMIT_LOOK - 1 before it, with its gain.
 */
__attribute__((optimize("O3"))) static float limit(const float sample)
{
    const float peak = fabsf(sample) * SPEECH_GAIN;
    const size_t pos = dac.limitPos;
    const size_t oldest = (pos + 1) % LIMIT_LOOK;

    dac.delayed[pos] = sample;
    dac.needed[pos] = (peak > LIMIT_PEAK) ? (SPEECH_GAIN * LIMIT_PEAK / peak) :
                                            SPEECH_GAIN;

    float least = dac.needed[0];
    for (size_t i = 1; i < LIMIT_LOOK; i++)
        least = MIN(least, dac.needed[i]);

    dac.held = MIN(least, dac.held + LIMIT_RELEASE);
    dac.eased[pos] = dac.held;
    dac.limitPos = (uint8_t)oldest;

    // Summed afresh each time: a running sum of floats drifts
    dac.easedSum = 0.0f;
    for (size_t i = 0; i < LIMIT_LOOK; i++)
        dac.easedSum += dac.eased[i];

    return dac.delayed[oldest] * (dac.easedSum / LIMIT_LOOK);
}

/**
 * Bring a half of the stream up to 48kHz, into the same half of DAC1's
 * samples. Done by the stream's writer, not in the interrupt: there it would
 * hold the UARTs' interrupts off for longer than a character lasts.
 *
 * @param ctx: stream.
 * @param half: 0 or 1.
 */
__attribute__((optimize("O3"))) static void upsample(struct streamCtx *ctx,
                                                     size_t half)
{
    const size_t size = ctx->bufSize / 2;
    const stream_sample_t *in = &ctx->buffer[half * size];
    uint16_t *out = &dac.out[half * size * UP];

    for (size_t n = 0; n < size; n++) {
        memmove(&dac.history[1], &dac.history[0],
                (PHASE_TAPS - 1) * sizeof(float));
        dac.history[0] = limit(lowPass(in[n]) * SPEECH_UNITY);

        for (size_t phase = 0; phase < UP; phase++) {
            float acc = 0.0f;
            for (size_t k = 0; k < PHASE_TAPS; k++)
                acc += interpolate[phase][k] * dac.history[k];

            const int32_t v = DAC_MID + (int32_t)acc;
            *out++ = (uint16_t)CLAMP(v, 0, 4095);
        }
    }
}

void at32Dac_isr(void)
{
    const uint32_t sts = sys_read32(DMA2_STS) & (DMA_C3_HDT | DMA_C3_FDT);
    if (sts == 0)
        return;

    sys_write32(sts, DMA2_CLR);
    if (dac.ctx == NULL)
        return;

    dac.playing = (sts & DMA_C3_FDT) ? 0 : 1;
    if (dac.stopping) {
        halt();
        dac.ctx->running = 0;
        dac.ctx = NULL;
        dac.stopping = false;
    }

    k_sem_give(&dac.sync);
}

/**
 * Start playing a speech stream from DAC1.
 */
static int dac_start(const uint8_t instance, const void *config,
                     struct streamCtx *ctx)
{
    (void)instance;
    (void)config;

    if ((ctx == NULL) || (ctx->running != 0) || (dac.ctx != NULL))
        return -EBUSY;
    if ((ctx->sampleRate != SPEECH_HZ) || (ctx->bufMode != BUF_CIRC_DOUBLE)
        || ((ctx->bufSize % 2) != 0))
        return -EINVAL;

    const size_t count = UP * ctx->bufSize;
    free(dac.out);
    dac.out = malloc(count * sizeof(uint16_t));
    if (dac.out == NULL)
        return -ENOMEM;

    halt();
    dac.stopping = false;
    dac.playing = 0;
    dac.idle = 1;
    memset(dac.history, 0, sizeof(dac.history));
    memset(dac.lpHistory, 0, sizeof(dac.lpHistory));
    limitReset();
    for (size_t i = 0; i < count; i++)
        dac.out[i] = DAC_MID;
    k_sem_init(&dac.sync, 0, 1);
    ctx->running = 1;
    dac.ctx = ctx;

    gpioPin_setMode(&pa4, ANALOG);
    sys_set_bits(CRM_APB1EN, APB1_TMR6 | APB1_DAC);
    sys_set_bits(CRM_AHBEN, AHB_DMA2);
    // TEN1 and DMAEN1 change with DAC1 off, as halt() left it
    sys_write32(0, TMR6_DIV);
    sys_write32(TIMER_HZ / DAC_HZ - 1, TMR6_PR);
    sys_write32(TMR6_TRGO_OVF, TMR6_CTRL2);
    sys_write32(1, TMR6_SWEVT);

    sys_write32(DAC_DHR12R1, DMA2_C3_PADDR);
    sys_write32((uint32_t)dac.out, DMA2_C3_MADDR);
    sys_write32(count, DMA2_C3_DTCNT);
    sys_write32(DMA_CTRL_DAC, DMA2_C3_CTRL);

    sys_write32(DAC_MID, DAC_DHR12R1);
    sys_set_bits(DAC_CTRL, DAC_CTRL_DMA1);
    sys_write32(1, TMR6_CTRL1);
    return 0;
}

/**
 * The half of the stream buffer that is not being played.
 */
static int dac_data(struct streamCtx *ctx, stream_sample_t **buf)
{
    const size_t half = ctx->bufSize / 2;

    dac.idle = 1 - dac.playing;
    *buf = &ctx->buffer[dac.idle * half];
    return (int)half;
}

/**
 * Take the half just written, if one was, and wait for DAC1 to pass a half
 * of the stream buffer.
 */
static int dac_sync(struct streamCtx *ctx, uint8_t dirty)
{
    if (dac.ctx != ctx)
        return -1;

    if (dirty && !dac.stopping)
        upsample(ctx, dac.idle);

    const int ret = (k_sem_take(&dac.sync, K_MSEC(200)) == 0) ? 0 : -1;

    // The interrupt ended the stream: DAC1 has stopped reading its samples
    if (dac.ctx == NULL) {
        free(dac.out);
        dac.out = NULL;
    }

    return ret;
}

/**
 * Stop at the stream's next half: the stream's last sync waits for it.
 */
static void dac_stop(struct streamCtx *ctx)
{
    if (dac.ctx != ctx)
        return;

    // The sync that follows then waits for the interrupt to end the stream
    const unsigned int key = irq_lock();
    k_sem_reset(&dac.sync);
    dac.stopping = true;
    irq_unlock(key);
}

/**
 * Stop at once.
 */
static void dac_terminate(struct streamCtx *ctx)
{
    if (dac.ctx != ctx)
        return;

    const unsigned int key = irq_lock();
    halt();
    dac.ctx = NULL;
    dac.stopping = false;
    ctx->running = 0;
    irq_unlock(key);
    free(dac.out);
    dac.out = NULL;
    k_sem_give(&dac.sync);
}

const struct audioDriver at32_dac_audio_driver = {
    .start = dac_start,
    .data = dac_data,
    .sync = dac_sync,
    .stop = dac_stop,
    .terminate = dac_terminate,
};
