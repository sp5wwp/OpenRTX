/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef BENCH_H
#define BENCH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bench PTT, pressed from the debug monitor ("ptt"): the radio transmits
 * through the normal OpenRTX path, as with the PTT key.
 *
 * @return true while the bench PTT is pressed.
 */
bool bench_getPtt(void);

/**
 * Transmit dry run, set from the debug monitor ("txdry"): the transmitter is
 * keyed with the PA drive (APC) held at zero, to check the keying sequence.
 *
 * @return true while the dry run is set.
 */
bool bench_txDryRun(void);

/**
 * Transmit PA drive override, set from the debug monitor ("txdrv"), for
 * measurements below the lowest calibrated power. It is never above the
 * calibrated drive.
 *
 * @return the PA drive (DAC2) to use instead of the calibration, 0 for none.
 */
uint8_t bench_txDrive(void);

/**
 * Take the PA drive to zero at once, from any context (timer, interrupt):
 * the bench PTT does it when it expires, in case the radio thread does not
 * end the transmission. The RF path switches are not touched.
 */
void bench_paOff(void);

/**
 * Measure the MCU clock against a BK4829 tone and correct the audio sample
 * rate (clockcal.h). Uses the UHF chip, left off afterwards: the next
 * reception start sets it up again. Skipped while the speaker is on.
 */
void bench_calibrateClock(void);

#ifdef __cplusplus
}
#endif

#endif // BENCH_H
