/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <stdint.h>
static inline void sys_write32(uint32_t v, uintptr_t a) { (void)v; (void)a; }
static inline uint32_t sys_read32(uintptr_t a) { (void)a; return 0; }
static inline void sys_set_bits(uintptr_t a, uint32_t m) { (void)a; (void)m; }
static inline void sys_clear_bits(uintptr_t a, uint32_t m) { (void)a; (void)m; }
