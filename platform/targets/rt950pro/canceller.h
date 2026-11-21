/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CANCELLER_H
#define CANCELLER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Sizes of the canceller model and filter, see canceller.c */
#define CANCELLER_NA 6
#define CANCELLER_NB 8
#define CANCELLER_TAPS 32

/**
 * Canceller coefficients: model of the DAC contribution to the audio node
 * (A, B) and correction filter (W). Compiled-in defaults, which the bench
 * monitor can overwrite.
 */
extern float canceller_A[CANCELLER_NA];
extern float canceller_B[CANCELLER_NB];
extern float canceller_W[CANCELLER_TAPS];

/**
 * Hook the canceller into the ADC and DAC drivers, which must be initialised.
 * The removal of the DAC contribution from the ADC samples starts at once;
 * the correction output, with canceller_enable().
 */
void canceller_init();

/**
 * Enable or disable the correction output.
 *
 * @param enable: true to enable it.
 */
void canceller_enable(bool enable);

/**
 * Whether the correction output is enabled.
 *
 * @return true if enabled.
 */
bool canceller_isEnabled(void);

#ifdef __cplusplus
}
#endif

#endif // CANCELLER_H
