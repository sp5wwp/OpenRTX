/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPTIONBYTES_H
#define OPTIONBYTES_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Tell whether the MCU option bytes select 224 kB of SRAM, see optionbytes.c.
 *
 * @return true if they do.
 */
bool optionbytes_sram224();

/**
 * Select 224 kB of SRAM (instead of 96 kB) in the MCU option bytes, keeping
 * every other option byte as it is. Takes effect at the next reset.
 *
 * @return 0 on success, a negative error code otherwise.
 */
int optionbytes_setSram224();

#ifdef __cplusplus
}
#endif

#endif // OPTIONBYTES_H
