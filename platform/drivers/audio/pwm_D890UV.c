/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <math.h>
#include <hwconfig.h>
#include "interfaces/delays.h"
#include "peripherals/gpio.h"
#include "pwm_D890UV.h"

/*
 * Audio output of the AnyTone AT-D890UV on PB9, as the stock firmware does
 * for its tones: TIM4 channel 4 runs an 8-bit PWM (625kHz at 160MHz), RC
 * filtered on the board (TONE net). TIM7 interrupts at the stream sample rate
 * and writes the next sample. The PWM idles at mid-scale between streams:
 * the TX path is DC coupled down to well below 1Hz, so a level step would
 * pull the carrier for seconds.
 *
 * Beeps use the same timer: a sine from a table at 48kHz, while no stream
 * runs. Streams to the speaker and beeps are scaled by a gain set with the
 * volume knob, as the TONE path does not go through the volume device.
 */

#define PWM_TOP 0xFFu
#define PWM_MID 0x80u

static struct streamCtx *stream;
static volatile size_t readPos;
static volatile bool syncPoint;
static volatile bool stopReq;
static volatile int32_t gain = 256; // Sample gain, 256 = unity

/*
 * Extra factor for 16-bit stream samples: 1 maps full scale to the full PWM
 * range (modulator). Speaker streams (voice prompts, Codec2 speech) use 24
 * and a soft limiter, y = 127 x / (|x| + 64): speech has a high peak to
 * average ratio, and linear gain only clipped its peaks without making it
 * louder.
 */
#define MULT_FULL 1
#define MULT_SPEECH 24
#define SOFT_KNEE 64
static volatile int32_t streamMult = MULT_FULL;

#define BEEP_RATE_HZ 48000
#define BEEP_TABLE 256

static int8_t sineTable[BEEP_TABLE];
static volatile bool beepOn;
static volatile uint32_t beepPhase;
static volatile uint32_t beepStep;
static volatile uint32_t beepCount; // Samples since the beep start
static volatile uint32_t beepDelay; // Silent samples before the tone
static volatile uint32_t beepEnd;   // Sample at which to stop, 0 = playing

static inline void setIdle()
{
    TIM4->CCR4 = PWM_MID;
}

static void stopStream()
{
    TIM7->CR1 = 0;
    TIM7->DIER = 0;
    setIdle();
    stream->running = 0;
}

void _Z15TIM7_IRQHandlerv()
{
    TIM7->SR = 0;

    if (beepOn) {
        beepCount += 1;
        if ((beepEnd != 0) && (beepCount >= beepEnd)) {
            beepOn = false;
            TIM7->CR1 = 0;
            TIM7->DIER = 0;
            setIdle();
            return;
        }

        if (beepCount <= beepDelay) {
            setIdle();
            return;
        }

        beepPhase += beepStep;
        TIM4->CCR4 = (int32_t)PWM_MID
                   + ((sineTable[beepPhase >> 24] * gain) >> 8);
        return;
    }

    if (stream == NULL)
        return;

    /* 16-bit signed sample to the 8-bit PWM range, times the gain */
    int32_t x = (stream->buffer[readPos] * gain * streamMult) >> 16;
    if (streamMult != MULT_FULL) {
        int32_t ax = (x < 0) ? -x : x;
        x = (127 * x) / (ax + SOFT_KNEE);
    }
    int32_t v = (int32_t)PWM_MID + x;
    if (v < 0)
        v = 0;
    if (v > (int32_t)PWM_TOP)
        v = PWM_TOP;
    TIM4->CCR4 = v;

    size_t prev = readPos;
    readPos += 1;

    bool sync = false;
    if (stream->bufMode == BUF_CIRC_DOUBLE) {
        size_t half = stream->bufSize / 2;
        if ((prev < half) && (readPos >= half))
            sync = true;
    }

    if (readPos >= stream->bufSize) {
        readPos = 0;
        sync = true;
        if (stream->bufMode == BUF_LINEAR)
            stopReq = true;
    }

    if (sync) {
        syncPoint = true;
        if (stopReq)
            stopStream();
    }
}

void pwmD890UV_init()
{
    for (int i = 0; i < BEEP_TABLE; i++)
        sineTable[i] =
            (int8_t)(127.0f * sinf(2.0f * 3.14159265f * i / BEEP_TABLE));

    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM4EN | RCC_APB1ENR1_TIM7EN;
    __DSB();

    TIM4->CR1 = 0;
    TIM4->PSC = 0;
    TIM4->ARR = PWM_TOP;
    TIM4->CCMR2 = (6 << TIM_CCMR2_OC4M_Pos) | TIM_CCMR2_OC4PE;
    TIM4->CCER = TIM_CCER_CC4E;
    setIdle();
    TIM4->EGR = TIM_EGR_UG;
    TIM4->CR1 = TIM_CR1_CEN;
    gpio_setMode(AUDIO_PWM, ALTERNATE | ALTERNATE_FUNC(2));

    TIM7->CR1 = 0;
    TIM7->PSC = 0;
    NVIC_SetPriority(TIM7_IRQn, 10);
    NVIC_ClearPendingIRQ(TIM7_IRQn);
    NVIC_EnableIRQ(TIM7_IRQn);
}

void pwmD890UV_terminate()
{
    NVIC_DisableIRQ(TIM7_IRQn);
    TIM7->CR1 = 0;
    TIM4->CR1 = 0;
    gpio_setMode(AUDIO_PWM, INPUT);
    RCC->APB1ENR1 &= ~(RCC_APB1ENR1_TIM4EN | RCC_APB1ENR1_TIM7EN);
}

void pwmD890UV_setGain(const uint16_t newGain)
{
    gain = newGain;
}

void pwmD890UV_beepStart(const uint16_t freq, const uint16_t delayMs)
{
    if ((stream != NULL) && (stream->running != 0))
        return;

    TIM7->DIER = 0;
    beepStep = (uint32_t)(((uint64_t)freq << 32) / BEEP_RATE_HZ);
    beepPhase = 0;
    beepCount = 0;
    beepDelay = ((uint32_t)delayMs * BEEP_RATE_HZ) / 1000;
    beepEnd = 0;
    beepOn = true;

    TIM7->CR1 = 0;
    TIM7->ARR = (SystemCoreClock / BEEP_RATE_HZ) - 1;
    TIM7->EGR = TIM_EGR_UG;
    TIM7->SR = 0;
    TIM7->DIER = TIM_DIER_UIE;
    TIM7->CR1 = TIM_CR1_CEN;
}

void pwmD890UV_beepStop()
{
    if ((beepOn == false) || (beepEnd != 0))
        return;

    // Play the tone for as long as requested, after the start delay
    uint32_t end = beepCount + beepDelay;
    if (end == 0)
        end = 1;
    beepEnd = end;
}

bool pwmD890UV_beepActive()
{
    return beepOn;
}

static int pwmD890UV_start(const uint8_t instance, const void *config,
                           struct streamCtx *ctx)
{
    (void)instance;

    if ((ctx == NULL) || (ctx->running != 0) || (ctx->sampleRate == 0))
        return -EINVAL;

    if ((stream != NULL) && (stream->running != 0))
        return -EBUSY;

    // A stream takes over a beep. Speaker streams follow the volume gain,
    // the others (modulator) are unscaled.
    beepOn = false;
    const struct pwmD890UVcfg *cfg = config;
    if ((cfg == NULL) || (cfg->speaker == false)) {
        gain = 256;
        streamMult = MULT_FULL;
    } else {
        streamMult = MULT_SPEECH;
    }

    ctx->running = 1;
    stream = ctx;
    readPos = 0;
    syncPoint = false;
    stopReq = false;

    TIM7->CR1 = 0;
    TIM7->ARR = (SystemCoreClock / ctx->sampleRate) - 1;
    TIM7->EGR = TIM_EGR_UG;
    TIM7->SR = 0;
    TIM7->DIER = TIM_DIER_UIE;
    TIM7->CR1 = TIM_CR1_CEN;

    return 0;
}

static int pwmD890UV_idleBuf(struct streamCtx *ctx, stream_sample_t **buf)
{
    if (ctx->bufMode != BUF_CIRC_DOUBLE) {
        *buf = NULL;
        return 0;
    }

    /* While the first half plays the second one is free, and vice versa */
    if (readPos < (ctx->bufSize / 2))
        *buf = ctx->buffer + (ctx->bufSize / 2);
    else
        *buf = ctx->buffer;

    return ctx->bufSize / 2;
}

static int pwmD890UV_sync(struct streamCtx *ctx, uint8_t dirty)
{
    (void)dirty;

    if (ctx->running == 0)
        return -1;

    /* A sync point already passed: the caller is late (overrun) */
    if (syncPoint) {
        syncPoint = false;
        return -1;
    }

    while ((syncPoint == false) && (ctx->running != 0))
        sleepFor(0, 1);

    syncPoint = false;
    return 0;
}

static void pwmD890UV_stop(struct streamCtx *ctx)
{
    if (ctx->running == 0)
        return;

    stopReq = true;
}

static void pwmD890UV_halt(struct streamCtx *ctx)
{
    if (ctx->running == 0)
        return;

    stopStream();
}

#pragma GCC diagnostic ignored "-Wpedantic"
const struct audioDriver pwmD890UV_audio_driver = {
    .start = pwmD890UV_start,
    .data = pwmD890UV_idleBuf,
    .sync = pwmD890UV_sync,
    .stop = pwmD890UV_stop,
    .terminate = pwmD890UV_halt,
};
#pragma GCC diagnostic pop
