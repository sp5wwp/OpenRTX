/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef AT32_ADC_H
#define AT32_ADC_H

#include "interfaces/audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Input streams of the Radtel RT-950 Pro, both from PA2, the MCU's tap on
 * the audio net the BK4829s' AF outputs share. ADC3 samples it, triggered by
 * TMR3 and read out by DMA2 channel 5; ADC1 stays the battery's.
 */

/**
 * Low IF of the receiver: the chip is tuned this much below the signal, a
 * sixth of PA2's 48kHz rate. The IF reaches the speaker on the shared net as
 * a hiss behind the decoded speech. 12kHz (a quarter of the rate, also
 * supported by the mixer) would put more of it above the speech band, but
 * the chip's AF output passes it about 10dB weaker than 8kHz, with its RF
 * filter at its widest too, and a signal there was not decoded (bench).
 */
#define AT32_ADC_LOW_IF_HZ 8000

enum at32AdcInstance {
    /**
     * The microphone at the stream's rate: one BK4829 passes its mic input
     * to its AF output while the other transmits. The chip's pre-emphasis
     * stays on, lifting the speech over the ADC's noise, and is undone here.
     * Samples are sixteen to an ADC count around zero.
     */
    AT32_ADC_MIC = 0,
    /**
     * An FM signal at the AT32_ADC_LOW_IF_HZ IF, demodulated to a 24kHz
     * baseband: the receiving chip puts its mixer's output on the pin, tuned
     * AT32_ADC_LOW_IF_HZ below the signal. The net's coupling takes the low frequencies of a
     * baseband demodulated in the chip, and a tenth of M17's symbols with
     * them; at the IF it takes nothing, and the band below 3.5kHz is left
     * for speech played onto the same net.
     */
    AT32_ADC_LOW_IF = 1,
};

/** PA2 input driver, its instances an at32AdcInstance. */
extern const struct audioDriver at32_adc_audio_driver;

/**
 * Handle the ADC DMA's half and full transfer interrupts.
 */
void at32Adc_isr(void);

#ifdef __cplusplus
}
#endif

#endif /* AT32_ADC_H */
