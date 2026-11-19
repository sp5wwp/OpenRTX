/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HWCONFIG_H
#define HWCONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Module17 hardware info flags, required by Module17 UI emulator target */
enum Mod17Flags {
    MOD17_FLAGS_HMI_PRESENT = 1,
    MOD17_FLAGS_SOFTPOT = 2,
};

/* Screen has adjustable brightness */
#define CONFIG_SCREEN_BRIGHTNESS

/* Anti-aliased fonts on the colour emulator; the pixel format comes from the
 * build. */
#ifdef CONFIG_PIX_FMT_RGB565
#define CONFIG_FONT_AA
#endif

/* Battery type */
#define CONFIG_BAT_LIPO
#define CONFIG_BAT_NCELLS 2

/* Device supports M17 mode */
#define CONFIG_M17

#ifdef __cplusplus
}
#endif

/* Message inbox and M17 SMS. */
#define CONFIG_MESSAGES
#define CONFIG_M17_SMS

/* The keypad keys come from the build: the variants model different radios */

#endif /* HWCONFIG_H */
