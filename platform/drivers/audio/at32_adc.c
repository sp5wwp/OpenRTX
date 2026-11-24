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
#include "at32_adc.h"

// AT32F403A registers: ADC3 on PA2, triggered by TMR3 channel 1 and read out
// by DMA2 channel 5
#define CRM_AHBEN 0x40021014
#define CRM_APB2EN 0x40021018
#define CRM_APB1EN 0x4002101C
#define AHB_DMA2 BIT(1)
#define APB2_ADC3 BIT(15)
#define APB1_TMR3 BIT(1)
#define ADC3_CTRL2 0x40013C08
#define ADC3_SPT2 0x40013C10
#define ADC3_OSQ1 0x40013C2C
#define ADC3_OSQ3 0x40013C34
#define ADC3_ODT 0x40013C4C
#define ADC_CTRL2_ADCEN BIT(0)
#define ADC_CTRL2_ADCAL BIT(2)
#define ADC_CTRL2_ADCALINIT BIT(3)
#define ADC_CTRL2_OCDMAEN BIT(8)
#define ADC_CTRL2_OCTEN BIT(20) // With trigger select 0, TMR3 channel 1
#define ADC_CHANNEL 2           // PA2
// 71.5 ADC clock cycles of sampling: 11us a conversion, for 48kHz
#define ADC_SPT2_PA2 (6 << (3 * ADC_CHANNEL))
#define TMR3_CTRL1 0x40000400
#define TMR3_SWEVT 0x40000414
#define TMR3_CM1 0x40000418
#define TMR3_CCTRL 0x40000420
#define TMR3_DIV 0x40000428
#define TMR3_PR 0x4000042C
#define TMR3_C1DT 0x40000434
#define TMR_CM1_C1_PWM_A (6 << 4)
#define TMR_CCTRL_C1EN BIT(0)
#define DMA2_STS 0x40020400
#define DMA2_CLR 0x40020404
#define DMA2_C5_CTRL 0x40020458
#define DMA2_C5_DTCNT 0x4002045C
#define DMA2_C5_PADDR 0x40020460
#define DMA2_C5_MADDR 0x40020464
#define DMA_C5_FDT BIT(17)
#define DMA_C5_HDT BIT(18)
#define DMA_C5_ALL (0xF << 16)
// Enabled, full and half transfer interrupts, peripheral to memory,
// circular, memory increment, 16 bit each way, high priority
#define DMA_CTRL_ADC                                                   \
    (BIT(0) | BIT(1) | BIT(2) | BIT(5) | BIT(7) | (1 << 8) | (1 << 10) \
     | (2 << 12))

#define TIMER_HZ 120000000   // APB1 at 60MHz, its timers at twice that

#define MIC_DEEMPH_HZ 300.0f // The chip's pre-emphasis starts here
#define MIC_SHIFT 4          // Sixteen to an ADC count

#define IF_RATE 48000        // PA2's rate for the low-IF demodulator
#define IF_OVERSAMPLE 2      // PA2 samples to one of the stream
#define IF_DC_POLE 32339     // DC blocker's pole, Q15
#define IF_SCALE 8000.0f     // Demodulated sample for a radian of phase step
#define IF_FIRST_HALF 8      // Half of the first filter's taps, and the centre
#define IF_FIRST_TAPS (2 * IF_FIRST_HALF - 1)
#define IF_SECOND_HALF 25
#define IF_SECOND_TAPS (2 * IF_SECOND_HALF - 1)

// The mixer's oscillator, at the IF
#if (AT32_ADC_LOW_IF_HZ * 4) == IF_RATE
static const float mixCos[4] = { 1.0f, 0.0f, -1.0f, 0.0f };
static const float mixSin[4] = { 0.0f, 1.0f, 0.0f, -1.0f };
#elif (AT32_ADC_LOW_IF_HZ * 6) == IF_RATE
static const float mixCos[6] = { 1.000000f,  0.500000f,  -0.500000f,
                                 -1.000000f, -0.500000f, 0.500000f };
static const float mixSin[6] = { 0.000000f, 0.866025f,  0.866025f,
                                 0.000000f, -0.866025f, -0.866025f };
#else
#error "AT32_ADC_LOW_IF_HZ: a quarter or a sixth of IF_RATE"
#endif
// 6.5kHz low-pass at 48kHz, 15 taps, Kaiser 5: the mixer's image, which
// halving the rate would fold onto the signal
static const float firstTaps[IF_FIRST_HALF] = { -0.000535f, -0.006249f,
                                                -0.015398f, -0.009309f,
                                                0.038274f,  0.130560f,
                                                0.227749f,  0.269815f };
// 4.4kHz low-pass at 24kHz, 49 taps, Kaiser 5: speech on the net, which the
// mixer puts 5kHz and more from the carrier. 29dB down on speech at 3kHz and
// 55dB at 2.7kHz; with 25 taps it was 11dB and 16dB, and speech cost a third
// of the frames of a weak signal. A high-pass ahead of the mixer or a
// recursive low-pass distorts the signal's edge next to the speech
static const float secondTaps[IF_SECOND_HALF] = {
    0.000286f,  0.000784f,  0.000251f, -0.001393f, -0.002043f,
    0.000328f,  0.003875f,  0.003477f, -0.002654f, -0.008095f,
    -0.004043f, 0.008102f,  0.013939f, 0.001847f,  -0.018401f,
    -0.020676f, 0.006410f,  0.036628f, 0.027053f,  -0.028847f,
    -0.074312f, -0.031642f, 0.116388f, 0.289506f,  0.366466f,
};

static const struct gpioPin pa2 = { &GpioA, ADC_CHANNEL };

static struct {
    struct streamCtx *ctx; ///< Running stream, NULL when none
    struct k_sem sync;     ///< Given when a half of the stream is ready
    uint8_t instance;      ///< An at32AdcInstance
    uint8_t ready;         ///< Half of the stream buffer last completed
    bool cooked;           ///< That half is speech already
    // Microphone
    int32_t micDc;      ///< Mean of the samples, Q8
    int32_t micLowPass; ///< De-emphasis state, Q8
    int32_t micPole;    ///< De-emphasis coefficient, Q15
    // Low IF
    uint16_t *raw;                ///< PA2 at IF_RATE, two halves, on the heap
    int32_t prev;                 ///< Last PA2 sample, -1 before the first
    int64_t blocked;              ///< DC blocker's output, Q8
    float ai[2 * IF_FIRST_TAPS];  ///< Mixed samples, each twice
    float aq[2 * IF_FIRST_TAPS];
    float bi[2 * IF_SECOND_TAPS]; ///< The same at half the rate
    float bq[2 * IF_SECOND_TAPS];
    size_t headA;
    size_t headB;
    uint8_t phase; ///< Mixer phase
    bool odd;      ///< This sample is dropped by the halving
    float lastI;
    float lastQ;
} adc;

/**
 * A linear-phase FIR's output from its first half of taps and its centre
 * one, the samples paired from both ends.
 *
 * @param taps: half of the taps then the centre one.
 * @param half: how many those are.
 * @param x: the 2 * half - 1 newest samples.
 * @return the filtered sample.
 */
__attribute__((optimize("O3"))) static float
symmetricFir(const float *taps, size_t half, const float *x)
{
    float acc = taps[half - 1] * x[half - 1];
    for (size_t k = 0; k < (half - 1); k++)
        acc += taps[k] * (x[k] + x[2 * half - 2 - k]);

    return acc;
}

/**
 * Angle of a vector, within 1.2e-5 rad (Abramowitz and Stegun 4.4.49).
 * atan2f() at every sample took more of the CPU than both filters.
 *
 * @param y: the vector's sine side.
 * @param x: its cosine side.
 * @return the angle in radians, -pi to pi.
 */
__attribute__((optimize("O3"))) static inline float angle(const float y,
                                                          const float x)
{
    const float ay = fabsf(y);
    const float ax = fabsf(x);
    const float hi = MAX(ax, ay);
    if (hi == 0.0f)
        return 0.0f;

    const float z = MIN(ax, ay) / hi;
    const float z2 = z * z;
    float a =
        z
        * (0.9998660f
           + z2
                 * (-0.3302995f
                    + z2 * (0.1801410f + z2 * (-0.0851330f + z2 * 0.0208351f))));
    if (ay > ax)
        a = 1.5707963f - a;
    if (x < 0.0f)
        a = 3.1415927f - a;

    return (y < 0.0f) ? -a : a;
}

/**
 * Demodulate PA2's samples from the IF (AT32_ADC_LOW_IF_HZ): mixed down, low-passed and halved in rate, low-passed again, and the phase
 * step between samples is the deviation.
 *
 * @param in: IF_OVERSAMPLE * count samples at IF_RATE.
 * @param out: demodulated samples.
 * @param count: how many to put out.
 */
__attribute__((optimize("O3"))) static void
demodulate(const uint16_t *in, stream_sample_t *out, size_t count)
{
    for (size_t n = 0; n < (IF_OVERSAMPLE * count); n++) {
        if (adc.prev < 0)
            adc.prev = in[n];
        adc.blocked = ((int64_t)(in[n] - adc.prev) << 8)
                    + ((adc.blocked * IF_DC_POLE) >> 15);
        adc.prev = in[n];

        // Each sample twice, a filter's length apart, so its newest samples
        // are contiguous
        const float v = (float)(int32_t)(adc.blocked >> 8);
        adc.ai[adc.headA] = v * mixCos[adc.phase];
        adc.aq[adc.headA] = -v * mixSin[adc.phase];
        adc.ai[adc.headA + IF_FIRST_TAPS] = adc.ai[adc.headA];
        adc.aq[adc.headA + IF_FIRST_TAPS] = adc.aq[adc.headA];
        adc.phase = (adc.phase + 1) % ARRAY_SIZE(mixCos);
        adc.headA = (adc.headA + 1) % IF_FIRST_TAPS;
        adc.odd = !adc.odd;
        if (adc.odd)
            continue;

        adc.bi[adc.headB] = symmetricFir(firstTaps, IF_FIRST_HALF,
                                         &adc.ai[adc.headA]);
        adc.bq[adc.headB] = symmetricFir(firstTaps, IF_FIRST_HALF,
                                         &adc.aq[adc.headA]);
        adc.bi[adc.headB + IF_SECOND_TAPS] = adc.bi[adc.headB];
        adc.bq[adc.headB + IF_SECOND_TAPS] = adc.bq[adc.headB];
        adc.headB = (adc.headB + 1) % IF_SECOND_TAPS;

        const float fi = symmetricFir(secondTaps, IF_SECOND_HALF,
                                      &adc.bi[adc.headB]);
        const float fq = symmetricFir(secondTaps, IF_SECOND_HALF,
                                      &adc.bq[adc.headB]);
        // The angle from the last filtered sample to this one
        const float cross = fq * adc.lastI - fi * adc.lastQ;
        const float dot = fi * adc.lastI + fq * adc.lastQ;
        adc.lastI = fi;
        adc.lastQ = fq;

        *out++ = (stream_sample_t)(angle(cross, dot) * IF_SCALE);
    }
}

/**
 * Turn a half of the stream buffer from PA2's counts into speech: the mean
 * removed, the chip's pre-emphasis undone, sixteen to a count.
 *
 * @param ctx: stream.
 * @param half: 0 or 1.
 */
static void microphone(struct streamCtx *ctx, int half)
{
    const size_t size = ctx->bufSize / 2;
    stream_sample_t *buf = &ctx->buffer[half * size];

    for (size_t n = 0; n < size; n++) {
        const int32_t x = (int32_t)(uint16_t)buf[n] << 8;
        if (adc.micDc < 0)
            adc.micDc = x;
        adc.micDc += (x - adc.micDc) >> 10;
        adc.micLowPass +=
            (int32_t)(((int64_t)(x - adc.micDc - adc.micLowPass) * adc.micPole)
                      >> 15);

        const int32_t out = adc.micLowPass >> (8 - MIC_SHIFT);
        buf[n] = (stream_sample_t)CLAMP(out, INT16_MIN, INT16_MAX);
    }
}

void at32Adc_isr(void)
{
    const uint32_t sts = sys_read32(DMA2_STS) & (DMA_C5_HDT | DMA_C5_FDT);
    if (sts == 0)
        return;

    sys_write32(sts, DMA2_CLR);
    if (adc.ctx == NULL)
        return;

    for (int half = 0; half < 2; half++) {
        if ((sts & (half ? DMA_C5_FDT : DMA_C5_HDT)) == 0)
            continue;

        // The samples are worked on in adc_data(), off the interrupt:
        // demodulating here would hold the UARTs' interrupts off for longer
        // than a character lasts
        adc.ready = half;
        adc.cooked = false;
        k_sem_give(&adc.sync);
    }
}

/**
 * Stop the timer, the DMA and the ADC's trigger.
 */
static void halt(void)
{
    sys_write32(0, TMR3_CTRL1);
    sys_write32(0, DMA2_C5_CTRL);
    sys_write32(DMA_C5_ALL, DMA2_CLR);
    sys_clear_bits(ADC3_CTRL2, ADC_CTRL2_OCTEN | ADC_CTRL2_OCDMAEN);
}

/**
 * Start sampling PA2 for a stream.
 */
static int adc_start(const uint8_t instance, const void *config,
                     struct streamCtx *ctx)
{
    (void)config;

    if ((ctx == NULL) || (ctx->running != 0) || (adc.ctx != NULL))
        return -EBUSY;
    if ((ctx->bufMode != BUF_CIRC_DOUBLE) || ((ctx->bufSize % 2) != 0))
        return -EINVAL;
    if ((instance == AT32_ADC_LOW_IF) && (ctx->sampleRate != IF_RATE / 2))
        return -EINVAL;

    const bool lowIf = (instance == AT32_ADC_LOW_IF);
    if (lowIf) {
        adc.raw = malloc(IF_OVERSAMPLE * ctx->bufSize * sizeof(uint16_t));
        if (adc.raw == NULL)
            return -ENOMEM;
    }

    const uint32_t rate = lowIf ? IF_RATE : ctx->sampleRate;

    adc.instance = instance;
    adc.ready = 0;
    adc.cooked = true;
    adc.micDc = -1;
    adc.micLowPass = 0;
    adc.micPole =
        (int32_t)(32768.0f
                  * (1.0f - expf(-6.2831853f * MIC_DEEMPH_HZ / (float)rate)));
    adc.prev = -1;
    adc.blocked = 0;
    memset(adc.ai, 0, sizeof(adc.ai));
    memset(adc.aq, 0, sizeof(adc.aq));
    memset(adc.bi, 0, sizeof(adc.bi));
    memset(adc.bq, 0, sizeof(adc.bq));
    adc.headA = 0;
    adc.headB = 0;
    adc.phase = 0;
    adc.odd = false;
    adc.lastI = 0.0f;
    adc.lastQ = 0.0f;
    k_sem_init(&adc.sync, 0, 1);

    gpioPin_setMode(&pa2, ANALOG);
    sys_set_bits(CRM_APB2EN, APB2_ADC3);
    sys_set_bits(CRM_APB1EN, APB1_TMR3);
    sys_set_bits(CRM_AHBEN, AHB_DMA2);

    // Power the ADC up and calibrate it once
    if ((sys_read32(ADC3_CTRL2) & ADC_CTRL2_ADCEN) == 0) {
        sys_write32(ADC_CTRL2_ADCEN, ADC3_CTRL2);
        k_busy_wait(100);
        sys_set_bits(ADC3_CTRL2, ADC_CTRL2_ADCALINIT);
        while (sys_read32(ADC3_CTRL2) & ADC_CTRL2_ADCALINIT)
            ;
        sys_set_bits(ADC3_CTRL2, ADC_CTRL2_ADCAL);
        while (sys_read32(ADC3_CTRL2) & ADC_CTRL2_ADCAL)
            ;
    }
    sys_write32(ADC_SPT2_PA2, ADC3_SPT2);
    sys_write32(0, ADC3_OSQ1);
    sys_write32(ADC_CHANNEL, ADC3_OSQ3);

    sys_write32(0, DMA2_C5_CTRL);
    sys_write32(DMA_C5_ALL, DMA2_CLR);
    sys_write32(ADC3_ODT, DMA2_C5_PADDR);
    sys_write32((uint32_t)(lowIf ? (void *)adc.raw : (void *)ctx->buffer),
                DMA2_C5_MADDR);
    sys_write32(lowIf ? (IF_OVERSAMPLE * ctx->bufSize) : ctx->bufSize,
                DMA2_C5_DTCNT);

    ctx->running = 1;
    adc.ctx = ctx;
    sys_write32(DMA_CTRL_ADC, DMA2_C5_CTRL);
    sys_set_bits(ADC3_CTRL2, ADC_CTRL2_OCTEN | ADC_CTRL2_OCDMAEN);

    // Channel 1's output, never on a pin, rises once a period
    sys_write32(0, TMR3_CTRL1);
    sys_write32(0, TMR3_DIV);
    sys_write32(TIMER_HZ / rate - 1, TMR3_PR);
    sys_write32(TMR_CM1_C1_PWM_A, TMR3_CM1);
    sys_write32(1, TMR3_C1DT);
    sys_write32(TMR_CCTRL_C1EN, TMR3_CCTRL);
    sys_write32(1, TMR3_SWEVT);
    sys_write32(1, TMR3_CTRL1);
    return 0;
}

/**
 * The half of the stream buffer completed last.
 */
static int adc_data(struct streamCtx *ctx, stream_sample_t **buf)
{
    const size_t half = ctx->bufSize / 2;

    if (!adc.cooked) {
        if (adc.instance == AT32_ADC_LOW_IF)
            demodulate(&adc.raw[adc.ready * IF_OVERSAMPLE * half],
                       &ctx->buffer[adc.ready * half], half);
        else
            microphone(ctx, adc.ready);

        adc.cooked = true;
    }

    *buf = &ctx->buffer[adc.ready * half];
    return (int)half;
}

/**
 * Wait for the next half of the stream buffer.
 */
static int adc_sync(struct streamCtx *ctx, uint8_t dirty)
{
    (void)dirty;

    if (ctx->running == 0)
        return -1;

    return (k_sem_take(&adc.sync, K_MSEC(200)) == 0) ? 0 : -1;
}

/**
 * Stop sampling at once: a half sample of speech or baseband is no use.
 */
static void adc_terminate(struct streamCtx *ctx)
{
    if ((ctx->running == 0) || (adc.ctx != ctx))
        return;

    const unsigned int key = irq_lock();
    halt();
    adc.ctx = NULL;
    ctx->running = 0;
    irq_unlock(key);
    free(adc.raw);
    adc.raw = NULL;
    k_sem_give(&adc.sync);
}

const struct audioDriver at32_adc_audio_driver = {
    .start = adc_start,
    .data = adc_data,
    .sync = adc_sync,
    .stop = adc_terminate,
    .terminate = adc_terminate,
};
