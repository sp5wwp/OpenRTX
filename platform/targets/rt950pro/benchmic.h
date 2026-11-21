/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef BENCHMIC_H
#define BENCHMIC_H

#include <stddef.h>
#include <stdint.h>
#include "interfaces/audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bench microphone: the radio has no microphone input for the MCU (PA0 only
 * carries a rectified level), so digital mode transmissions take their audio
 * from a clip loaded in RAM from the debug monitor, played in a loop.
 * Without a clip, the stream carries silence.
 */

/**
 * Hook the bench microphone into the DAC driver, which paces it.
 */
void benchMic_init(void);

/**
 * Allocate the clip, replacing the previous one.
 *
 * @param samples: clip length in samples (8 kHz, 16 bit).
 * @return 0 on success, -ENOMEM if it does not fit.
 */
int benchMic_alloc(size_t samples);

/**
 * Write samples into the clip.
 *
 * @param offset: first sample.
 * @param data: samples.
 * @param count: number of samples.
 * @return 0 on success, -EINVAL if outside the clip.
 */
int benchMic_write(size_t offset, const int16_t *data, size_t count);

/**
 * Audio driver of the bench microphone. The stream sample rate must divide
 * the DAC driver rate (48 kHz); the 8 kHz clip samples are repeated at higher
 * rates.
 */
extern const struct audioDriver benchMic_driver;

#ifdef __cplusplus
}
#endif

#endif // BENCHMIC_H
