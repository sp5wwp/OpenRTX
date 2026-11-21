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
    TESTSIGNAL_NOISE = 3,  ///< White noise, uniform, frequency ignored.
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

/**
 * Record an ADC2 channel, sampled from the same TMR6 interrupt as the test
 * signal, so that the recording is synchronous with it. Next to each sample
 * the DAC1 value written in the same interrupt is recorded. Blocks until the
 * recording is complete; the test signal being generated, if any, goes on.
 *
 * @param channel: ADC channel, 0 for PA0 (microphone level) to 15.
 * @param count: number of samples, at most TESTSIGNAL_REC_MAX.
 * @param div: one sample every div periods of the 48 kHz timer (1-48).
 * @param samples: set to the recorded samples, 12 bit right-aligned.
 * @param dacSamples: set to the DAC1 values, 12 bit right-aligned.
 * @return 0 on success, a negative error code otherwise.
 */
int testsignal_record(uint8_t channel, uint32_t count, uint32_t div,
                      const uint16_t **samples, const uint16_t **dacSamples);

/** Maximum number of samples of a recording */
#define TESTSIGNAL_REC_MAX 4096

#ifdef __cplusplus
}
#endif

#endif // TESTSIGNAL_H
