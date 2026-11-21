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
 * Placement of the functions on the 48 kHz interrupt path: section
 * .audio_isr, which the target links within the zero-wait Flash. With 224 kB
 * of SRAM only the first 128 kB of Flash are zero-wait (AT32F403A reference
 * manual 2.3), and code there runs faster than from SRAM, where instruction
 * fetches compete with data accesses (bench: about 1.5 times).
 */
#define AUDIO_ISR_CODE __attribute__((noinline, section(".audio_isr")))

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
 * Function run on behalf of the DAC interrupt at a normal interrupt priority,
 * for the kernel calls the DAC interrupt cannot make (see dacAt32_defer()).
 */
typedef void (*dacAt32_deferred)(void);

/**
 * Initialise DAC1 (PA4) and its sample timer, TMR6. The DAC outputs
 * mid-scale until a stream is started.
 */
void dacAt32_init(void);

/**
 * Maximum number of per-sample hooks.
 */
#define DAC_AT32_MAX_HOOKS 4

/**
 * Add a per-sample hook. Hooks run in the order they were added, each one
 * receiving the value the previous one returned.
 *
 * @param hook: hook function.
 * @return 0 on success, -ENOMEM if all the hook slots are in use.
 */
int dacAt32_addHook(dacAt32_hook hook);

/**
 * Set the function called at each sample period, or remove it with NULL.
 *
 * @param tick: tick function.
 */
void dacAt32_setTick(dacAt32_tick tick);

/**
 * Maximum number of deferred functions.
 */
#define DAC_AT32_MAX_DEFERRED 4

/**
 * Register a deferred function.
 *
 * @param fn: deferred function.
 * @return its identifier for dacAt32_defer(), -ENOMEM if all the slots are in
 * use.
 */
int dacAt32_addDeferred(dacAt32_deferred fn);

/**
 * From the DAC interrupt (hooks, tick): run a deferred function, at a lower
 * priority, as soon as the interrupt returns. Kernel calls (e.g. giving a
 * semaphore) go there, so that the DAC interrupt stays short and regular.
 *
 * @param id: identifier returned by dacAt32_addDeferred().
 */
void dacAt32_defer(int id);

/**
 * Mask the DAC interrupt, and all the interrupts irq_lock() masks, to update
 * state shared with it.
 *
 * @return key for dacAt32_unlock().
 */
unsigned int dacAt32_lock(void);

/**
 * Unmask the interrupts masked by dacAt32_lock().
 *
 * @param key: key returned by dacAt32_lock().
 */
void dacAt32_unlock(unsigned int key);

/**
 * Audio driver for output streams on DAC1. Instance and configuration are
 * not used.
 */
extern const struct audioDriver dacAt32_driver;

#ifdef __cplusplus
}
#endif

#endif // DAC_AT32_H
