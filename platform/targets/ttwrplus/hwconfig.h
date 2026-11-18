/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HWCONFIG_H
#define HWCONFIG_H

#include <zephyr/device.h>

/*
 * Display properties are encoded in the devicetree
 */
#define DISPLAY DT_CHOSEN(zephyr_display)
#define CONFIG_SCREEN_WIDTH DT_PROP(DISPLAY, width)
#define CONFIG_SCREEN_HEIGHT DT_PROP(DISPLAY, height)
#define CONFIG_PIX_FMT_BW

#define CONFIG_GPS
#define CONFIG_NMEA_RBUF_SIZE 128

#define CONFIG_BAT_LIPO
#define CONFIG_BAT_NCELLS 1

#define CONFIG_M17

/* Keypad input capability (adaptive text entry): arrows = has
 * LEFT/RIGHT keys; numeric = has a 0-9 keypad (ETSI multi-tap);
 * space-on-hash = the space key is '#' (else '0'). */
#define CONFIG_KBD_HAS_ARROWS 0
#define CONFIG_KBD_HAS_NUMERIC 0
#define CONFIG_KBD_SPACE_ON_HASH 0
#endif /* HWCONFIG_H */
