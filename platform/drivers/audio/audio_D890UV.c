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
 * - the microphone source samples PA7 (mic_D890UV.c), which needs no set-up;
 *   AnyTone's M17 proposal uses band B's chip and PC0 instead (NOTES.md
 *   3.8);
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

static const struct pwmD890UVcfg pwmModulator = { .speaker = false };
static const struct pwmD890UVcfg pwmSpeaker = { .speaker = true };

const struct audioDevice outputDevices[] = {
    { NULL, 0, 0, SINK_MCU },
    { &pwmD890UV_audio_driver, &pwmModulator, 0, SINK_RTX },
    { &pwmD890UV_audio_driver, &pwmSpeaker, 0, SINK_SPK },
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

void platform_updateMcuAudioGain();
bool radio_afActive();
void radio_muteRxAudio(bool mute);

/*
 * Speaker amplifier for MCU audio. It needs time to wake up: short key beeps
 * started together with it were lost (only its switch-on pop was heard). A
 * beep therefore waits until the amplifier has been on for AMP_WAKEUP_MS, and
 * the amplifier stays on AMP_HANG_MS after MCU audio ends, so that a series
 * of beeps does not pop each time.
 */
#define AMP_WAKEUP_MS 100
#define AMP_HANG_MS 2000

static long long ampOnTick;
static long long mcuOffTick;
static bool mcuSpk;
static bool rxSpk; // Receive path connected (FM with the squelch open)
static bool hangPending;

static void ampOn()
{
    if (gpio_readPin(SPK_EN) == 0) {
        gpio_setPin(SPK_EN);
        ampOnTick = getTick();
    }
}

uint16_t audio_ampWakeupDelay()
{
    long long elapsed = getTick() - ampOnTick;
    if ((gpio_readPin(SPK_EN) == 0) || (elapsed < 0))
        return AMP_WAKEUP_MS;
    if (elapsed >= AMP_WAKEUP_MS)
        return 0;

    return AMP_WAKEUP_MS - elapsed;
}

void audio_poll()
{
    if ((hangPending == false) || mcuSpk || pwmD890UV_beepActive())
        return;

    if ((getTick() - mcuOffTick) < AMP_HANG_MS)
        return;

    hangPending = false;
    if ((rxSpk == false) && (radio_afActive() == false))
        gpio_clearPin(SPK_EN);
}

void audio_connect(const enum AudioSource source, const enum AudioSink sink)
{
    if (sink != SINK_SPK)
        return;

    if (source == SOURCE_MCU) {
        platform_updateMcuAudioGain();
        mcuSpk = true;
        hangPending = false;
        ampOn();
    } else if (source == SOURCE_RTX) {
        rxSpk = true;
        radio_muteRxAudio(false);
        ampOn();
    }
}

void audio_disconnect(const enum AudioSource source, const enum AudioSink sink)
{
    if (sink != SINK_SPK)
        return;

    if (source == SOURCE_MCU) {
        // Switched off later by audio_poll()
        mcuSpk = false;
        mcuOffTick = getTick();
        hangPending = true;
    } else if (source == SOURCE_RTX) {
        // Suspended by a voice prompt or closed: no receive audio under it
        rxSpk = false;
        radio_muteRxAudio(true);
        if ((mcuSpk == false) && (hangPending == false))
            gpio_clearPin(SPK_EN);
    }
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
