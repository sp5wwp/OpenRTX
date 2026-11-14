/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PWM_D890UV_H
#define PWM_D890UV_H

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
 * Set up the PWM, idling at mid-scale.
 */
void pwmD890UV_init();

/**
 * Stop the PWM and release PB9.
 */
void pwmD890UV_terminate();

#ifdef __cplusplus
}
#endif

#endif // PWM_D890UV_H
