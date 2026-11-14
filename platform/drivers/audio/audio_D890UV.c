/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <string.h>
#include "interfaces/audio.h"
#include "interfaces/delays.h"
#include "peripherals/gpio.h"
#include "pwm_D890UV.h"
#include "mic_D890UV.h"
#include "hwconfig.h"

/*
 * Audio routing of the AnyTone AT-D890UV:
 * - the received audio of band A reaches the speaker amplifier through a
 *   routing switch set by the radio driver;
 * - the MCU drives PB9 (PWM, pwm_D890UV.c), RC filtered to the TONE net. It
 *   reaches the speaker through the AF-REP summing node (band B chip GPO1
 *   low) and, in TX, band A's modulator (switches set by the radio driver);
 * - the microphone source samples PA7 (mic_D890UV.c), the only microphone
 *   signal wired to the MCU; no speech was seen there on the development
 *   radio (NOTES.md 3.8);
 * - the receive baseband (PC0) has no driver yet: it also delivers paced
 *   silence, as a stream that fails at once would make the M17 receiver
 *   loop without blocking and starve the other threads.
 */

/*
 * Silent source: a circular double buffer of zeros, with sync points paced by
 * the system tick at the requested sample rate. One state per instance.
 */
enum silenceInstance {
    SILENCE_MIC = 0,
    SILENCE_RTX = 1,
    SILENCE_NUM,
};

struct silenceState {
    long long nextSync;
    long long halfMs;
    bool secondHalf;
};

static struct silenceState silence[SILENCE_NUM];

static int silence_start(const uint8_t instance, const void *config,
                         struct streamCtx *ctx)
{
    (void)config;

    if ((instance >= SILENCE_NUM) || (ctx == NULL) || (ctx->running != 0)
        || (ctx->sampleRate == 0) || (ctx->bufMode != BUF_CIRC_DOUBLE))
        return -EINVAL;

    struct silenceState *st = &silence[instance];

    memset(ctx->buffer, 0, ctx->bufSize * sizeof(stream_sample_t));
    st->halfMs = ((long long)(ctx->bufSize / 2) * 1000) / ctx->sampleRate;
    if (st->halfMs == 0)
        st->halfMs = 1;

    st->nextSync = getTick() + st->halfMs;
    st->secondHalf = false;
    ctx->priv = st;
    ctx->running = 1;

    return 0;
}

static int silence_data(struct streamCtx *ctx, stream_sample_t **buf)
{
    const struct silenceState *st = ctx->priv;

    /* The half acquired last */
    *buf = st->secondHalf ? ctx->buffer : ctx->buffer + (ctx->bufSize / 2);
    return ctx->bufSize / 2;
}

static int silence_sync(struct streamCtx *ctx, uint8_t dirty)
{
    (void)dirty;

    if (ctx->running == 0)
        return -1;

    struct silenceState *st = ctx->priv;

    sleepUntil(st->nextSync);
    st->nextSync += st->halfMs;
    st->secondHalf = !st->secondHalf;

    return 0;
}

static void silence_stop(struct streamCtx *ctx)
{
    ctx->running = 0;
}

#pragma GCC diagnostic ignored "-Wpedantic"
static const struct audioDriver silence_audio_driver = {
    .start = silence_start,
    .data = silence_data,
    .sync = silence_sync,
    .stop = silence_stop,
    .terminate = silence_stop,
};
#pragma GCC diagnostic pop

const struct audioDevice outputDevices[] = {
    { NULL, 0, 0, SINK_MCU },
    { &pwmD890UV_audio_driver, 0, 0, SINK_RTX },
    { &pwmD890UV_audio_driver, 0, 0, SINK_SPK },
};

const struct audioDevice inputDevices[] = {
    { NULL, 0, 0, SOURCE_MCU },
    { &silence_audio_driver, 0, SILENCE_RTX, SOURCE_RTX },
    { &micD890UV_audio_driver, 0, 0, SOURCE_MIC },
};

void audio_init()
{
    gpio_clearPin(SPK_EN);
    gpio_setMode(SPK_EN, OUTPUT);
    pwmD890UV_init();
}

void audio_terminate()
{
    gpio_clearPin(SPK_EN);
    pwmD890UV_terminate();
}

void audio_connect(const enum AudioSource source, const enum AudioSink sink)
{
    if ((sink == SINK_SPK)
        && ((source == SOURCE_RTX) || (source == SOURCE_MCU)))
        gpio_setPin(SPK_EN);
}

void audio_disconnect(const enum AudioSource source, const enum AudioSink sink)
{
    if ((sink == SINK_SPK)
        && ((source == SOURCE_RTX) || (source == SOURCE_MCU)))
        gpio_clearPin(SPK_EN);
}

bool audio_checkPathCompatibility(const enum AudioSource p1Source,
                                  const enum AudioSink p1Sink,
                                  const enum AudioSource p2Source,
                                  const enum AudioSink p2Sink)
{
    /*
     * Each source and each sink is a single piece of hardware here (PB9
     * serves both the speaker and the modulator, but only as one MCU source):
     * two paths can run together if they share neither.
     */
    return (p1Source != p2Source) && (p1Sink != p2Sink);
}
