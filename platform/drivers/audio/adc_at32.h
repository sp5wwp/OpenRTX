/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ADC_AT32_H
#define ADC_AT32_H

#include <stdint.h>
#include "interfaces/audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise ADC3 for input streams on one of its channels. Conversions are
 * paced by the DAC driver sample clock (dac_at32.h), which must be
 * initialised.
 *
 * @param channel: ADC channel, 0 to 3 for PA0 to PA3.
 */
void adcAt32_init(uint8_t channel);

/**
 * Last ADC sample converted, 12 bit right-aligned, for per-sample processing
 * running in the DAC interrupt after the ADC tick.
 *
 * @return ADC sample.
 */
uint16_t adcAt32_lastSample(void);

/**
 * Audio driver for input streams on ADC3. The sample rate of a stream must
 * divide DAC_AT32_RATE; the samples are averaged down to it, and the DC level
 * is removed. Instance and configuration are not used.
 */
extern const struct audioDriver adcAt32_driver;

#ifdef __cplusplus
}
#endif

#endif // ADC_AT32_H
