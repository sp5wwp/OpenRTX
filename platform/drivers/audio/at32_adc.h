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

enum at32AdcInstance {
    /**
     * The microphone at the stream's rate: one BK4829 passes its mic input
     * to its AF output while the other transmits. The chip's pre-emphasis
     * stays on, lifting the speech over the ADC's noise, and is undone here.
     * Samples are sixteen to an ADC count around zero.
     */
    AT32_ADC_MIC = 0,
    /**
     * An FM signal at an 8kHz IF, demodulated to a 24kHz baseband: the
     * receiving chip puts its mixer's output on the pin, tuned 8kHz below
     * the signal. The net's coupling takes the low frequencies of a
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
