/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Check that the stock bootloader is the version the update hand-off was
 * made for.
 *
 * @return true if it is.
 */
bool bootloader_isKnown();

/**
 * Restart into the bootloader's update mode, as with PTT + top key at
 * power-on. Does not return.
 */
void bootloader_restartToUpdate();

/**
 * Hand over to the bootloader update mode if requested. Called first thing
 * by Reset_Handler.
 */
void early_boot_hook();

#ifdef __cplusplus
}
#endif

#endif // BOOTLOADER_H
