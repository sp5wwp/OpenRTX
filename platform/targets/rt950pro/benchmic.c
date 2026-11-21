/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <drivers/audio/dac_at32.h>
#include "benchmic.h"

/* Clip sample rate */
#define CLIP_RATE 8000

static int16_t *clip;
static size_t clipLen;
static size_t clipPos;

static struct streamCtx *volatile stream;
static volatile uint32_t pos;   /* Stream sample being written */
static volatile uint32_t sub;   /* DAC tick within it */
static volatile uint32_t ratio; /* DAC ticks per stream sample */
static volatile uint32_t hold;  /* Stream samples per clip sample */
static volatile uint32_t held;  /* Stream samples of the current clip sample */
static volatile bool stopRequest;
static int syncDeferred;
static K_SEM_DEFINE(syncSem, 0, 1);

static void syncEvent(void)
{
    k_sem_give(&syncSem);
}

static AUDIO_ISR_CODE stream_sample_t nextSample(void)
{
    if (clipLen == 0)
        return 0;

    stream_sample_t s = clip[clipPos];
    if (++held >= hold) {
        held = 0;
        clipPos = (clipPos + 1 < clipLen) ? (clipPos + 1) : 0;
    }

    return s;
}

/* DAC hook, used for its timing only: the output sample is not changed */
static AUDIO_ISR_CODE int32_t hook(int32_t sample)
{
    struct streamCtx *ctx = stream;
    if ((ctx == NULL) || (ctx->running == 0))
        return sample;
    if (++sub < ratio)
        return sample;

    sub = 0;
    ctx->buffer[pos++] = nextSample();

    bool half = (pos == (ctx->bufSize / 2));
    bool end = (pos >= ctx->bufSize);
    if (!half && !end)
        return sample;

    if (end)
        pos = 0;
    if (stopRequest || (end && (ctx->bufMode == BUF_LINEAR)))
        ctx->running = 0;

    dacAt32_defer(syncDeferred);

    return sample;
}

void benchMic_init(void)
{
    syncDeferred = dacAt32_addDeferred(syncEvent);
    dacAt32_addHook(hook);
}

int benchMic_alloc(size_t samples)
{
    unsigned int key = dacAt32_lock();
    int16_t *old = clip;
    clip = NULL;
    clipLen = 0;
    clipPos = 0;
    dacAt32_unlock(key);
    free(old);

    int16_t *buf = calloc(samples, sizeof(int16_t));
    if (buf == NULL)
        return -ENOMEM;

    key = dacAt32_lock();
    clip = buf;
    clipLen = samples;
    dacAt32_unlock(key);

    return 0;
}

int benchMic_write(size_t offset, const int16_t *data, size_t count)
{
    if ((offset > clipLen) || (count > clipLen - offset))
        return -EINVAL;

    memcpy(&clip[offset], data, count * sizeof(int16_t));

    return 0;
}

static int benchMic_start(const uint8_t instance, const void *config,
                          struct streamCtx *ctx)
{
    (void)instance;
    (void)config;

    if ((ctx == NULL) || (ctx->running != 0) || (ctx->sampleRate == 0))
        return -EINVAL;
    if (((DAC_AT32_RATE % ctx->sampleRate) != 0)
        || ((ctx->sampleRate % CLIP_RATE) != 0))
        return -EINVAL;
    if ((stream != NULL) && (stream->running != 0))
        return -EBUSY;

    unsigned int key = dacAt32_lock();
    pos = 0;
    sub = 0;
    ratio = DAC_AT32_RATE / ctx->sampleRate;
    hold = ctx->sampleRate / CLIP_RATE;
    held = 0;
    clipPos = 0;
    stopRequest = false;
    k_sem_reset(&syncSem);
    ctx->running = 1;
    stream = ctx;
    dacAt32_unlock(key);

    return 0;
}

static int benchMic_data(struct streamCtx *ctx, stream_sample_t **buf)
{
    size_t half = ctx->bufSize / 2;

    /* The ready half is the one not being filled */
    if (pos < half)
        *buf = &ctx->buffer[half];
    else
        *buf = &ctx->buffer[0];

    return half;
}

static int benchMic_sync(struct streamCtx *ctx, uint8_t dirty)
{
    (void)dirty;

    if (ctx->running == 0)
        return -1;

    k_sem_take(&syncSem, K_FOREVER);

    return 0;
}

static void benchMic_stop(struct streamCtx *ctx)
{
    if ((ctx != stream) || (ctx->running == 0))
        return;

    stopRequest = true;
}

static void benchMic_terminate(struct streamCtx *ctx)
{
    unsigned int key = dacAt32_lock();
    ctx->running = 0;
    bool current = (ctx == stream);
    if (current)
        stream = NULL;
    dacAt32_unlock(key);

    if (current)
        k_sem_give(&syncSem);
}

const struct audioDriver benchMic_driver = {
    .start = benchMic_start,
    .data = benchMic_data,
    .sync = benchMic_sync,
    .stop = benchMic_stop,
    .terminate = benchMic_terminate,
};
