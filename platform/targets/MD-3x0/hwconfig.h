/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HWCONFIG_H
#define HWCONFIG_H

#include "stm32f4xx.h"
#include "pinmap.h"

#ifdef __cplusplus
extern "C" {
#endif

enum adcChannel {
    ADC_VOL_CH = 0,
    ADC_VBAT_CH = 1,
    ADC_VOX_CH = 3,
    ADC_RSSI_CH = 8
};

extern const struct gpsDevice gps;
extern const struct spiDevice nvm_spi;
extern const struct spiCustomDevice pll_spi;
extern const struct spiCustomDevice c5000_spi;
extern const struct sky73210 pll;
extern const struct Adc adc1;

/* Device has a working real time clock */
#define CONFIG_RTC

/* Device supports an optional GPS chip */
#define CONFIG_GPS
#define CONFIG_GPS_STM32_USART3
#define CONFIG_NMEA_RBUF_SIZE 128

/* Device has a channel selection knob */
#define CONFIG_KNOB_ABSOLUTE

/* Screen dimensions */
#define CONFIG_SCREEN_WIDTH 160
#define CONFIG_SCREEN_HEIGHT 128

/* Screen pixel format */
#define CONFIG_PIX_FMT_RGB565

/* Draw the screen 16 rows at a time instead of keeping a framebuffer */
#define CONFIG_GFX_BAND_ROWS 16

/* Screen has adjustable brightness */
#define CONFIG_SCREEN_BRIGHTNESS

/* Battery type */
#define CONFIG_BAT_LIION
#define CONFIG_BAT_NCELLS 2

/* Device supports M17 mode */
#define CONFIG_M17

/* Microphone audio input */
#define CONFIG_MIC_GAIN 32
#define CONFIG_MIC_OVERSAMPLE 8

#ifdef __cplusplus
}
#endif

/* Message inbox and M17 SMS. */
#define CONFIG_MESSAGES
#define CONFIG_M17_SMS

/* Keypad input capability (adaptive text entry): arrows = has
 * LEFT/RIGHT keys; numeric = has a 0-9 keypad (ETSI multi-tap);
 * space-on-hash = the space key is '#' (else '0'). */
#define CONFIG_KBD_HAS_ARROWS 0
#define CONFIG_KBD_HAS_NUMERIC 1
#define CONFIG_KBD_SPACE_ON_HASH 0
#endif /* HWCONFIG_H */
