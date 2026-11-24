/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Host stubs for at32_adc.c: only demodulate() is used */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
struct k_sem { int unused; };
static inline void k_sem_init(struct k_sem *s, unsigned a, unsigned b) { (void)s; (void)a; (void)b; }
static inline void k_sem_give(struct k_sem *s) { (void)s; }
static inline int k_sem_take(struct k_sem *s, int t) { (void)s; (void)t; return 0; }
#define K_MSEC(x) (x)
#define K_FOREVER (-1)
#define BIT(n) (1UL << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define CLAMP(v, lo, hi) MIN(MAX((v), (lo)), (hi))
static inline void k_busy_wait(uint32_t us) { (void)us; }
static inline unsigned int irq_lock(void) { return 0; }
static inline void irq_unlock(unsigned int k) { (void)k; }
