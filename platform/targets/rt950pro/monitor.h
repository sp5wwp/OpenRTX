/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MONITOR_H
#define MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute one debug monitor command line, see monitor.c. The line is
 * modified.
 *
 * @param line: command line, without the line terminator.
 */
void monitor_exec(char *line);

#ifdef __cplusplus
}
#endif

#endif // MONITOR_H
