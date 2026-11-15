/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PWM_D890UV_H
#define PWM_D890UV_H

#include <stdbool.h>
#include "interfaces/audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Audio output driver of the AT-D890UV: TIM4 channel 4 PWM on PB9, one
 * sample per TIM7 interrupt.
 */
extern const struct audioDriver pwmD890UV_audio_driver;

/**
 * Device configuration: speaker streams follow the volume gain.
 */
struct pwmD890UVcfg {
    bool speaker;
};

/**
 * Set up the PWM, idling at mid-scale.
 */
void pwmD890UV_init();

/**
 * Stop the PWM and release PB9.
 */
void pwmD890UV_terminate();

/**
 * Set the gain of beeps and speaker streams, 256 = full scale.
 *
 * @param gain: new gain.
 */
void pwmD890UV_setGain(const uint16_t gain);

/**
 * Play a sine tone on PB9 while no stream runs, after a silent delay (to let
 * the speaker amplifier wake up).
 *
 * @param freq: tone frequency, in Hz.
 * @param delayMs: silence before the tone, in ms.
 */
void pwmD890UV_beepStart(const uint16_t freq, const uint16_t delayMs);

/**
 * Stop the tone started by pwmD890UV_beepStart(). After a start delay, the
 * tone still plays for the time it was requested, then stops.
 */
void pwmD890UV_beepStop();

/**
 * @return true while a tone plays or waits for its delay.
 */
bool pwmD890UV_beepActive();

#ifdef __cplusplus
}
#endif

#endif // PWM_D890UV_H
