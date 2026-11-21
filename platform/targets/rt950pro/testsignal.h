/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TESTSIGNAL_H
#define TESTSIGNAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Test signal shapes.
 */
enum testsignalShape {
    TESTSIGNAL_OFF = 0,    ///< DAC1 output at mid-scale, timer stopped.
    TESTSIGNAL_SINE = 1,   ///< Sine wave.
    TESTSIGNAL_SQUARE = 2, ///< Square wave.
};

/**
 * Generate a test signal on DAC1 (PA4), 48 kHz samples from TMR6. Replaces
 * the signal being generated, if any.
 *
 * @param shape: signal shape.
 * @param hz: signal frequency, in Hz.
 * @param amp: peak amplitude, in DAC counts around mid-scale (0-2047).
 * @return 0 on success, a negative error code otherwise.
 */
int testsignal_start(enum testsignalShape shape, uint32_t hz, uint32_t amp);

#ifdef __cplusplus
}
#endif

#endif // TESTSIGNAL_H
