/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DAC_AT32_H
#define DAC_AT32_H

#include <stdint.h>
#include "interfaces/audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Output sample rate of the DAC, in Hz. The sample rate of a stream must
 * divide it; slower streams are linearly interpolated up to it.
 */
#define DAC_AT32_RATE 48000

/**
 * Largest output value, in DAC counts around mid-scale.
 */
#define DAC_AT32_MAX 2047

/**
 * Hook called from the DAC interrupt for each output sample, at
 * DAC_AT32_RATE, for processing that has to run sample by sample.
 *
 * @param sample: stream sample due, in DAC counts around mid-scale, 0 when no
 * stream is running.
 * @return value to output, in DAC counts around mid-scale; it is clamped.
 */
typedef int32_t (*dacAt32_hook)(int32_t sample);

/**
 * Function called first in the DAC interrupt, at DAC_AT32_RATE, for drivers
 * that share its sample clock.
 */
typedef void (*dacAt32_tick)(void);

/**
 * Initialise DAC1 (PA4) and its sample timer, TMR6. The DAC outputs
 * mid-scale until a stream is started.
 */
void dacAt32_init(void);

/**
 * Set the per-sample hook, or remove it with NULL.
 *
 * @param hook: hook function.
 */
void dacAt32_setHook(dacAt32_hook hook);

/**
 * Set the function called at each sample period, or remove it with NULL.
 *
 * @param tick: tick function.
 */
void dacAt32_setTick(dacAt32_tick tick);

/**
 * Audio driver for output streams on DAC1. Instance and configuration are
 * not used.
 */
extern const struct audioDriver dacAt32_driver;

#ifdef __cplusplus
}
#endif

#endif // DAC_AT32_H
