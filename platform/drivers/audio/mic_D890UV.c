/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <hwconfig.h>
#include "interfaces/delays.h"
#include "peripherals/gpio.h"
#include "mic_D890UV.h"

/*
 * Microphone input of the AnyTone AT-D890UV on PA7 ("A/D DET FOR VOX", ADC1
 * channel 12), the microphone signal that needs no set-up. AnyTone's M17
 * proposal routes the microphone through band B's chip to PC0 instead, which
 * needs a band B set-up not known yet. On the development radio nothing was
 * seen on either (NOTES.md 3.8).
 *
 * ADC1 is shared with the battery and volume readings, which use software
 * started regular conversions: the microphone uses an injected conversion,
 * which interrupts a regular one and lets it resume. A TIM6 interrupt at the
 * sample rate stores the previous result and starts the next conversion, so
 * the CPU never waits for the ADC (391.5 cycle sampling, about 10us).
 */

static struct streamCtx *stream;
static volatile size_t writePos;
static volatile bool syncPoint;
static volatile bool stopReq;
static volatile bool firstSample;

static void stopStream()
{
    NVIC_DisableIRQ(TIM6_IRQn);
    TIM6->CR1 = 0;
    TIM6->DIER = 0;
    RCC->APB1ENR1 &= ~RCC_APB1ENR1_TIM6EN;
    stream->running = 0;
}

void _Z15TIM6_IRQHandlerv()
{
    TIM6->SR = 0;

    if ((stream == NULL) || (stream->running == 0))
        return;

    // Store the conversion started at the previous interrupt
    if (firstSample == false) {
        int32_t raw = (int32_t)(ADC1->JDR1 & 0x3FFF);
        stream->buffer[writePos] = (stream_sample_t)((raw - 8192) * 4);

        size_t prev = writePos;
        writePos += 1;

        bool sync = false;
        if (stream->bufMode == BUF_CIRC_DOUBLE) {
            size_t half = stream->bufSize / 2;
            if ((prev < half) && (writePos >= half))
                sync = true;
        }

        if (writePos >= stream->bufSize) {
            writePos = 0;
            sync = true;
            if (stream->bufMode == BUF_LINEAR)
                stopReq = true;
        }

        if (sync) {
            syncPoint = true;
            if (stopReq) {
                stopStream();
                return;
            }
        }
    }

    firstSample = false;
    ADC1->CR |= ADC_CR_JADSTART;
}

static int micD890UV_start(const uint8_t instance, const void *config,
                           struct streamCtx *ctx)
{
    (void)instance;
    (void)config;

    if ((ctx == NULL) || (ctx->running != 0) || (ctx->sampleRate == 0))
        return -EINVAL;

    if ((stream != NULL) && (stream->running != 0))
        return -EBUSY;

    gpio_setMode(AIN_MIC, ANALOG);

    // Injected sequence: one conversion of the microphone channel, started
    // by software (JEXTEN = 0). JSQR may only be written with JADSTART = 0.
    while ((ADC1->CR & ADC_CR_JADSTART) != 0)
        ;
    ADC1->PCSEL |= 1u << ADC_MIC_CH;
    ADC1->JSQR = ADC_MIC_CH << ADC_JSQR_JSQ1_Pos;

    ctx->running = 1;
    stream = ctx;
    writePos = 0;
    syncPoint = false;
    stopReq = false;
    firstSample = true;

    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;
    __DSB();
    TIM6->CR1 = 0;
    TIM6->PSC = 0;
    TIM6->ARR = (SystemCoreClock / ctx->sampleRate) - 1;
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR = 0;
    TIM6->DIER = TIM_DIER_UIE;
    NVIC_SetPriority(TIM6_IRQn, 10);
    NVIC_ClearPendingIRQ(TIM6_IRQn);
    NVIC_EnableIRQ(TIM6_IRQn);
    TIM6->CR1 = TIM_CR1_CEN;

    return 0;
}

static int micD890UV_data(struct streamCtx *ctx, stream_sample_t **buf)
{
    if (ctx->bufMode != BUF_CIRC_DOUBLE) {
        *buf = ctx->buffer;
        return ctx->bufSize;
    }

    // The half filled last: the other one is being written
    if (writePos < (ctx->bufSize / 2))
        *buf = ctx->buffer + (ctx->bufSize / 2);
    else
        *buf = ctx->buffer;

    return ctx->bufSize / 2;
}

static int micD890UV_sync(struct streamCtx *ctx, uint8_t dirty)
{
    (void)dirty;

    if (ctx->running == 0)
        return -1;

    while ((syncPoint == false) && (ctx->running != 0))
        sleepFor(0, 1);

    syncPoint = false;
    return 0;
}

static void micD890UV_stop(struct streamCtx *ctx)
{
    if (ctx->running == 0)
        return;

    stopReq = true;
}

static void micD890UV_halt(struct streamCtx *ctx)
{
    if (ctx->running == 0)
        return;

    stopStream();
}

#pragma GCC diagnostic ignored "-Wpedantic"
const struct audioDriver micD890UV_audio_driver = {
    .start = micD890UV_start,
    .data = micD890UV_data,
    .sync = micD890UV_sync,
    .stop = micD890UV_stop,
    .terminate = micD890UV_halt,
};
#pragma GCC diagnostic pop
