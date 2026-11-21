/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <hwconfig.h>
#include "interfaces/audio.h"
#include "interfaces/radio.h"
#include "adc_at32.h"
#include "dac_at32.h"
#include "canceller.h"

/*
 * Audio driver for the Radtel RT-950 Pro.
 *
 * Both BK4829 AF outputs drive the receive audio node, which feeds the speaker
 * amplifier (powered by PE4, enabled by PB8; PE1 routes it to the internal
 * speaker when low) and is read by the MCU on PA2. DAC1 (PA4) reaches either
 * that node, with PC12 low, or the BK4829 microphone input, with PC12 high
 * (bench measurements, see platform/targets/rt950pro). The radio driver keeps
 * the chip which is not receiving disconnected from the node.
 */

const struct audioDevice outputDevices[] = {
    { .driver = NULL, .config = NULL, .instance = 0, .endpoint = SINK_MCU },
    { .driver = NULL, .config = NULL, .instance = 0, .endpoint = SINK_RTX },
    {
        .driver = &dacAt32_driver,
        .config = NULL,
        .instance = 0,
        .endpoint = SINK_SPK,
    },
};

/* PA2, ADC channel 2: the receive audio node */
#define RX_AUDIO_ADC_CHANNEL 2

const struct audioDevice inputDevices[] = {
    { .driver = NULL, .config = NULL, .instance = 0, .endpoint = SOURCE_MCU },
    {
        .driver = &adcAt32_driver,
        .config = NULL,
        .instance = 0,
        .endpoint = SOURCE_RTX,
    },
    { .driver = NULL, .config = NULL, .instance = 0, .endpoint = SOURCE_MIC },
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

    gpioPin_clear(&rt950_audio_sw); // DAC to the receive audio node
    gpioPin_setMode(&rt950_audio_sw, OUTPUT);

    gpioPin_clear(&rt950_amp_en);
    gpioPin_setMode(&rt950_amp_en, OUTPUT);

    gpioPin_set(&rt950_amp_pwr);
    gpioPin_setMode(&rt950_amp_pwr, OUTPUT);

    dacAt32_init();
    adcAt32_init(RX_AUDIO_ADC_CHANNEL);
}

void audio_terminate()
{
    gpioPin_set(&rt950_spk_mute);
    gpioPin_clear(&rt950_amp_en);
    gpioPin_clear(&rt950_amp_pwr);
}

/* Active routes, by source and sink */
static bool paths[SOURCE_MCU + 1][SINK_MCU + 1];

/*
 * Keep the speaker on while any active route plays to it: a suspended route
 * is disconnected while the route taking over is already connected.
 */
static void updateSpeaker(void)
{
    bool speaker = false;

    for (size_t i = 0; i <= SOURCE_MCU; i++)
        speaker |= paths[i][SINK_SPK];

    if (speaker) {
        gpioPin_set(&rt950_amp_en);
        gpioPin_clear(&rt950_spk_mute);
    } else {
        gpioPin_set(&rt950_spk_mute);
        gpioPin_clear(&rt950_amp_en);
    }
}

/*
 * The receiving chip drives the audio node while any route takes audio from
 * it: to the speaker (FM) or to the MCU (digital modes, through the ADC).
 */
static bool rtxRouted(void)
{
    return paths[SOURCE_RTX][SINK_SPK] || paths[SOURCE_RTX][SINK_MCU];
}

/*
 * The receive audio canceller keeps the received baseband off the speaker
 * while the MCU samples it (digital modes); with the receiver audio routed to
 * the speaker (FM) that audio is the wanted one.
 */
static void updateCanceller(void)
{
    canceller_enable(paths[SOURCE_RTX][SINK_MCU]
                     && !paths[SOURCE_RTX][SINK_SPK]);
}

void audio_connect(const enum AudioSource source, const enum AudioSink sink)
{
    paths[source][sink] = true;

    if ((source == SOURCE_MCU) && (sink == SINK_SPK))
        gpioPin_clear(&rt950_audio_sw);
    if (source == SOURCE_RTX)
        radio_enableAfOutput();

    updateSpeaker();
    updateCanceller();
}

void audio_disconnect(const enum AudioSource source, const enum AudioSink sink)
{
    paths[source][sink] = false;
    updateSpeaker();
    updateCanceller();

    if ((source == SOURCE_RTX) && !rtxRouted())
        radio_disableAfOutput();
}

bool audio_checkPathCompatibility(const enum AudioSource p1Source,
                                  const enum AudioSink p1Sink,
                                  const enum AudioSource p2Source,
                                  const enum AudioSink p2Sink)
{
    /*
     * Physical sources and sinks are exclusive. MCU is a memory endpoint, so
     * separate capture and playback streams can share it.
     */
    return ((p1Source != p2Source) || (p1Source == SOURCE_MCU))
        && ((p1Sink != p2Sink) || (p1Sink == SINK_MCU));
}
