/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HWCONFIG_H
#define HWCONFIG_H

#include <at32f423.h>
#include "pinmap.h"

enum adcChannel {
    ADC_VBAT_CH = 20,
};

extern const struct spiCustomDevice display_spi;
extern const struct spiCustomDevice flash_spi;
extern const struct Adc adc1;

/* Screen dimensions */
#define CONFIG_SCREEN_WIDTH 128
#define CONFIG_SCREEN_HEIGHT 64

#define CONFIG_SCREEN_CONTRAST
#define CONFIG_DEFAULT_CONTRAST 91

/* Screen has adjustable brightness */
#define CONFIG_SCREEN_BRIGHTNESS

/* Screen pixel format */
#define CONFIG_PIX_FMT_BW

/* Battery type */
#define CONFIG_BAT_LIION
#define CONFIG_BAT_NCELLS 2

/* Keypad input capability (adaptive text entry): arrows = has
 * LEFT/RIGHT keys; numeric = has a 0-9 keypad (ETSI multi-tap);
 * space-on-hash = the space key is '#' (else '0'). */
#define CONFIG_KBD_HAS_ARROWS 0
#define CONFIG_KBD_HAS_NUMERIC 1
#define CONFIG_KBD_SPACE_ON_HASH 0

#endif
