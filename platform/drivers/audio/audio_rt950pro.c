/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <zephyr/irq.h>
#include <hwconfig.h>
#include "interfaces/audio.h"
#include "interfaces/radio.h"
#include "drivers/baseband/radio_rt950pro.h"
#include "drivers/baseband/BK4829_tone.h"
#include "at32_adc.h"
#include "at32_dac.h"

/*
 * Audio driver for the Radtel RT-950 Pro: FM reception to the speaker, and
 * the streams M17 runs on.
 *
 * The signal chain is: BK4829 AF output -> audio switch (PC12) -> amplifier
 * (powered by PE4, enabled by PB8) -> speaker, with PE1 muting the speaker.
 * The BK4829 chips share the same analog audio bus, the radio driver keeps the
 * chip which is not receiving disconnected from it. The MCU taps that bus on
 * PA2 and, with PC12 low, plays onto it from DAC1; PB8 high also takes the
 * microphone off its line.
 */

#define DMA2_CHANNEL3_IRQ 58
#define DMA2_CHANNEL4_5_IRQ 59

const struct audioDevice outputDevices[] = {
    { NULL, 0, 0, SINK_MCU },
    { &bk4829_tone_audio_driver, 0, 0, SINK_RTX },
    { &at32_dac_audio_driver, 0, 0, SINK_SPK },
};

const struct audioDevice inputDevices[] = {
    { NULL, 0, 0, SOURCE_MCU },
    { &at32_adc_audio_driver, 0, AT32_ADC_LOW_IF, SOURCE_RTX },
    { &at32_adc_audio_driver, 0, AT32_ADC_MIC, SOURCE_MIC },
};

/**
 * DMA2 channel 3's interrupt: DAC1.
 */
static void dacIsr(const void *arg)
{
    ARG_UNUSED(arg);
    at32Dac_isr();
}

/**
 * DMA2 channels 4 and 5's interrupt: the tone transmitter's bus and PA2.
 */
static void busAdcIsr(const void *arg)
{
    ARG_UNUSED(arg);
    bk4829Tone_isr();
    at32Adc_isr();
}

void audio_init()
{
    IRQ_CONNECT(DMA2_CHANNEL3_IRQ, 1, dacIsr, NULL, 0);
    IRQ_CONNECT(DMA2_CHANNEL4_5_IRQ, 0, busAdcIsr, NULL, 0);
    irq_enable(DMA2_CHANNEL3_IRQ);
    irq_enable(DMA2_CHANNEL4_5_IRQ);

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

    gpioPin_clear(&rt950_mic_mute);
    gpioPin_setMode(&rt950_mic_mute, OUTPUT);

    gpioPin_set(&rt950_amp_pwr);
    gpioPin_setMode(&rt950_amp_pwr, OUTPUT);
}

void audio_terminate()
{
    gpioPin_set(&rt950_spk_mute);
    gpioPin_clear(&rt950_mic_mute);
    gpioPin_clear(&rt950_amp_pwr);
}

void audio_connect(const enum AudioSource source, const enum AudioSink sink)
{
    // The microphone is off its line while the speaker plays
    if (source == SOURCE_RTX && sink == SINK_SPK) {
        radio_enableAfOutput();
        gpioPin_set(&rt950_mic_mute);
        gpioPin_clear(&rt950_spk_mute);
    } else if (source == SOURCE_MCU && sink == SINK_SPK) {
        gpioPin_clear(&rt950_audio_sw); // DAC1 onto the audio bus
        gpioPin_set(&rt950_mic_mute);
        gpioPin_clear(&rt950_spk_mute);
    } else if (source == SOURCE_MIC && sink == SINK_RTX) {
        // The microphone on its line, into the chip that transmits; DAC1,
        // which PC12 high puts on that line too, is off
        gpioPin_clear(&rt950_mic_mute);
    } else if (source == SOURCE_MIC && sink == SINK_MCU) {
        gpioPin_clear(&rt950_mic_mute); // The microphone on its line
        gpioPin_clear(&rt950_audio_sw); // and at full level on the MCU's tap
        radio_rt950pro_micCodec(true);
    }
}

void audio_disconnect(const enum AudioSource source, const enum AudioSink sink)
{
    if (source == SOURCE_RTX && sink == SINK_SPK) {
        gpioPin_set(&rt950_spk_mute);
        gpioPin_clear(&rt950_mic_mute);
        radio_disableAfOutput();
    } else if (source == SOURCE_MCU && sink == SINK_SPK) {
        gpioPin_set(&rt950_spk_mute);
        gpioPin_clear(&rt950_mic_mute);
        gpioPin_set(&rt950_audio_sw);
    } else if (source == SOURCE_MIC && sink == SINK_MCU) {
        radio_rt950pro_micCodec(false);
        gpioPin_set(&rt950_audio_sw);
    }
}

bool audio_checkPathCompatibility(const enum AudioSource p1Source,
                                  const enum AudioSink p1Sink,
                                  const enum AudioSource p2Source,
                                  const enum AudioSink p2Sink)
{
    /*
     * Physical sources and sinks are exclusive. The MCU is a memory
     * endpoint, so separate capture and playback streams can share it: the
     * microphone and the baseband out when transmitting M17, the baseband in
     * and the speech out when receiving it.
     */
    return ((p1Source != p2Source) || (p1Source == SOURCE_MCU))
        && ((p1Sink != p2Sink) || (p1Sink == SINK_MCU));
}
