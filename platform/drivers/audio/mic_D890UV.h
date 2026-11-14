/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MIC_D890UV_H
#define MIC_D890UV_H

#include "interfaces/audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Microphone input driver of the AT-D890UV: PA7 (ADC1 channel 12) sampled
 * with injected conversions started from a TIM6 interrupt.
 */
extern const struct audioDriver micD890UV_audio_driver;

#ifdef __cplusplus
}
#endif

#endif // MIC_D890UV_H
