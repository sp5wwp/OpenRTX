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
#include "drivers/baseband/SCT3288.h"

enum adcChannel {
    ADC_VBAT_CH = 2, // PC1, ADC1_IN2
    ADC_VOL_CH = 3,  // PC2, ADC1_IN3
};

extern const struct Adc adc1;
extern const struct wx4818n wx4818nA;
extern const struct wx4818n wx4818nB;
struct spiDevice;
extern const struct spiDevice sct_spi;
extern const struct sct3288 sct3288;
struct i2cDevice;
extern const struct i2cDevice i2c_bus;

/* Screen dimensions */
#define CONFIG_SCREEN_WIDTH 160
#define CONFIG_SCREEN_HEIGHT 128

/* Screen pixel format */
#define CONFIG_PIX_FMT_RGB565

/* Screen has adjustable brightness */
#define CONFIG_SCREEN_BRIGHTNESS

/*
 * FM transmission. Off until it has been checked into a dummy load: the
 * transmitter stays powered down whatever is requested.
 */
// #define CONFIG_D890UV_TX

/*
 * Splash screen time: the radio driver opens the digital board twice at
 * power-on, as the stock firmware does, which takes about 8.5s.
 */
#define CONFIG_SPLASH_TIME_MS 9500u

/* Battery type */
#define CONFIG_BAT_LIION
#define CONFIG_BAT_NCELLS 2

#endif // HWCONFIG_H
