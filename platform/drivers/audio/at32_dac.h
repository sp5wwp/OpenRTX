/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef AT32_DAC_H
#define AT32_DAC_H

#include "interfaces/audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Speaker output stream of the Radtel RT-950 Pro: 8kHz speech from DAC1
 * (PA4) onto the audio net, with PC12 low. DAC1 runs at 48kHz from TMR6 and
 * DMA2 channel 3 and the speech is brought up to that rate below 3kHz, so
 * that it stays clear of a signal the receiver takes from the same net at
 * its 8kHz IF.
 */
extern const struct audioDriver at32_dac_audio_driver;

/**
 * Bench: DAC1 holds its mid level instead of the speech, which then leaves
 * the net the receiver's IF is read from.
 */
extern volatile bool at32DacSilent;

/**
 * Handle the DAC DMA's half and full transfer interrupts.
 */
void at32Dac_isr(void);

#ifdef __cplusplus
}
#endif

#endif /* AT32_DAC_H */
