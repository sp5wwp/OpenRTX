/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HWCONFIG_H
#define HWCONFIG_H

#include <stm32u5xx.h>
#include "pinmap.h"
#include "drivers/baseband/WX4818N.h"

enum adcChannel {
    ADC_VBAT_CH = 2, // PC1, ADC1_IN2
    ADC_VOL_CH = 3,  // PC2, ADC1_IN3
};

extern const struct Adc adc1;
extern const struct wx4818n wx4818nA;
extern const struct wx4818n wx4818nB;

/* Screen dimensions */
#define CONFIG_SCREEN_WIDTH 160
#define CONFIG_SCREEN_HEIGHT 128

/* Screen pixel format */
#define CONFIG_PIX_FMT_RGB565

/* Screen has adjustable brightness */
#define CONFIG_SCREEN_BRIGHTNESS

/* Battery type */
#define CONFIG_BAT_LIION
#define CONFIG_BAT_NCELLS 2

#endif // HWCONFIG_H
