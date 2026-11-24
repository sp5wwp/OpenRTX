/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <stdint.h>
struct gpioDev { int unused; };
struct gpioPin { const struct gpioDev *port; uint8_t pin; };
enum Mode { INPUT, OUTPUT, ANALOG };
static inline int gpioPin_setMode(const struct gpioPin *p, int m) { (void)p; (void)m; return 0; }
