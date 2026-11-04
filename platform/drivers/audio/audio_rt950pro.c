/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <hwconfig.h>
#include "interfaces/audio.h"
#include "interfaces/radio.h"

/*
 * Audio driver for the Radtel RT-950 Pro, supporting only the reception path
 * from the BK4829 chips to the speaker.
 *
 * The signal chain is: BK4829 AF output -> audio switch (PC12) -> amplifier
 * (powered by PE4, enabled by PB8) -> speaker, with PE1 muting the speaker.
 * The BK4829 chips share the same analog audio bus, the radio driver keeps the
 * chip which is not receiving disconnected from it.
 */

const struct audioDevice outputDevices[] = {
    { NULL, 0, 0, SINK_MCU },
    { NULL, 0, 0, SINK_RTX },
    { NULL, 0, 0, SINK_SPK },
};

const struct audioDevice inputDevices[] = {
    { NULL, 0, 0, SINK_MCU },
    { NULL, 0, 0, SINK_RTX },
    { NULL, 0, 0, SINK_SPK },
};

void audio_init()
{
    /*
     * Levels are set before switching the pins to output, so that the
     * speaker is muted from the very first moment the pins are driven.
     */
    gpioPin_set(&rt950_spk_mute);
    gpioPin_setMode(&rt950_spk_mute, OUTPUT);

    gpioPin_clear(&rt950_bt_sw); // Speaker, not Bluetooth
    gpioPin_setMode(&rt950_bt_sw, OUTPUT);

    gpioPin_set(&rt950_audio_sw); // BK4829 chips, not the MCU DAC
    gpioPin_setMode(&rt950_audio_sw, OUTPUT);

    gpioPin_clear(&rt950_amp_en);
    gpioPin_setMode(&rt950_amp_en, OUTPUT);

    gpioPin_set(&rt950_amp_pwr);
    gpioPin_setMode(&rt950_amp_pwr, OUTPUT);
}

void audio_terminate()
{
    gpioPin_set(&rt950_spk_mute);
    gpioPin_clear(&rt950_amp_en);
    gpioPin_clear(&rt950_amp_pwr);
}

void audio_connect(const enum AudioSource source, const enum AudioSink sink)
{
    if (source == SOURCE_RTX && sink == SINK_SPK) {
        radio_enableAfOutput();
        gpioPin_set(&rt950_amp_en);
        gpioPin_clear(&rt950_spk_mute);
    }
}

void audio_disconnect(const enum AudioSource source, const enum AudioSink sink)
{
    if (source == SOURCE_RTX && sink == SINK_SPK) {
        gpioPin_set(&rt950_spk_mute);
        gpioPin_clear(&rt950_amp_en);
        radio_disableAfOutput();
    }
}

bool audio_checkPathCompatibility(const enum AudioSource p1Source,
                                  const enum AudioSink p1Sink,
                                  const enum AudioSource p2Source,
                                  const enum AudioSink p2Sink)
{
    static const uint8_t RTX_SPK = (SOURCE_RTX * 3) + SINK_SPK;
    static const uint8_t MIC_RTX = (SOURCE_MIC * 3) + SINK_RTX;

    const uint8_t p1 = (p1Source * 3) + p1Sink;
    const uint8_t p2 = (p2Source * 3) + p2Sink;

    // RTX-SPK and MIC-RTX are compatible, all the other combinations are not
    return ((p1 == RTX_SPK) && (p2 == MIC_RTX))
        || ((p1 == MIC_RTX) && (p2 == RTX_SPK));
}
