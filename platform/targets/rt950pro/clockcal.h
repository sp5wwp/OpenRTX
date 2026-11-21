/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CLOCKCAL_H
#define CLOCKCAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * MCU clock measurement, a bench tool. The MCU clock of the RT-950 Pro comes
 * from its 8 MHz HEXT crystal, while the BK4829 run from an accurate 26 MHz
 * TCXO. A tone of a BK4829, on the receive audio node, is measured on the ADC
 * samples taken at the audio ticks: the ratio is the error of the audio
 * sample rate. The crystal measured -5 ppm; samples lost by the audio
 * interrupt (dacAt32_lostTicks) show up as a larger, varying error (they once
 * made it look like -146 to -210 ppm). The samples are taken at the
 * DAC driver ticks, whose rate is corrected for dacAt32_getClockTrim(): the
 * result is the full MCU clock error, the trim included.
 */

/**
 * Hook the measurement into the DAC driver, which must be initialised.
 */
void clockcal_init(void);

/**
 * Measure the frequency of a tone on the receive audio node (PA2), against
 * the MCU clock. Blocks for about a second.
 *
 * @param toneHz: true frequency of the tone, Hz.
 * @param error: MCU clock error, 0.1 ppm units, negative when slow.
 * @return 0 on success, -EIO if the tone is too weak or unsteady.
 */
int clockcal_measure(float toneHz, int32_t *error);

/**
 * Last MCU clock error measured, 0.1 ppm units, 0 if none.
 *
 * @return MCU clock error.
 */
int32_t clockcal_lastError(void);

#ifdef __cplusplus
}
#endif

#endif // CLOCKCAL_H
