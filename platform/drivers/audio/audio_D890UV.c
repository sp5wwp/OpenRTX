/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "interfaces/audio.h"
#include "peripherals/gpio.h"
#include "hwconfig.h"

/*
 * Audio routing of the AnyTone AT-D890UV. Only the received audio towards the
 * speaker is supported for now: the WX4818N audio output reaches the LN4898
 * speaker amplifier, which is switched on when the path is connected.
 */

const struct audioDevice outputDevices[] =
{
    {NULL, 0, 0, SINK_MCU},
    {NULL, 0, 0, SINK_RTX},
    {NULL, 0, 0, SINK_SPK},
};

const struct audioDevice inputDevices[] =
{
    {NULL, 0, 0, SINK_MCU},
    {NULL, 0, 0, SINK_RTX},
    {NULL, 0, 0, SINK_SPK},
};

void audio_init()
{
    gpio_clearPin(SPK_EN);
    gpio_setMode(SPK_EN, OUTPUT);
}

void audio_terminate()
{
    gpio_clearPin(SPK_EN);
}

void audio_connect(const enum AudioSource source, const enum AudioSink sink)
{
    if ((source == SOURCE_RTX) && (sink == SINK_SPK))
        gpio_setPin(SPK_EN);
}

void audio_disconnect(const enum AudioSource source, const enum AudioSink sink)
{
    if ((source == SOURCE_RTX) && (sink == SINK_SPK))
        gpio_clearPin(SPK_EN);
}

bool audio_checkPathCompatibility(const enum AudioSource p1Source,
                                  const enum AudioSink   p1Sink,
                                  const enum AudioSource p2Source,
                                  const enum AudioSink   p2Sink)
{
    (void) p1Source;
    (void) p1Sink;
    (void) p2Source;
    (void) p2Sink;

    return false;
}
