/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef BENCH_H
#define BENCH_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bench PTT of the debug monitor (monitor.c), ORed with the PTT buttons.
 *
 * @return true while the monitor keys the radio.
 */
bool bench_getPtt(void);

#ifdef __cplusplus
}
#endif

#endif // BENCH_H
