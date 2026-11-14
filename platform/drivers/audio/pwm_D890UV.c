/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
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
 */

#define PWM_TOP 0xFFu
#define PWM_MID 0x80u

static struct streamCtx *stream;
static volatile size_t readPos;
static volatile bool syncPoint;
static volatile bool stopReq;

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

    if (stream == NULL)
        return;

    /* 16-bit signed sample to the 8-bit PWM range */
    int32_t v = (int32_t)PWM_MID + (stream->buffer[readPos] >> 8);
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

static int pwmD890UV_start(const uint8_t instance, const void *config,
                           struct streamCtx *ctx)
{
    (void)instance;
    (void)config;

    if ((ctx == NULL) || (ctx->running != 0) || (ctx->sampleRate == 0))
        return -EINVAL;

    if ((stream != NULL) && (stream->running != 0))
        return -EBUSY;

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
