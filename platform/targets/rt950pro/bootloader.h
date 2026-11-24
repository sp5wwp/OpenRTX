/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Restart into the stock bootloader, for a firmware update over the
 * programming cable without pressing keys. The host has to send the
 * bootloader's "enter update" frame right after the restart, see
 * bootloader.c. Does not return.
 */
void bootloader_restartToUpdate();

#ifdef __cplusplus
}
#endif

#endif // BOOTLOADER_H
