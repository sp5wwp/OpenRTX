/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef C62_REMOTE_H
#define C62_REMOTE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Longest reply text, see baseband_dump_send_text() */
#define CTRL_TEXT_MAX 250

/**
 * Switch the PTT pad to UART2 RXD and start the remote control thread.
 * Call after baseband_dump_init(). See c62_remote.c for the commands.
 */
void c62_remote_init(void);

/**
 * Set by the remote control after it changed the channel: the UI thread
 * then sends the new configuration to the rtx thread.
 */
extern volatile bool ui_rtxSyncRequest;

#ifdef __cplusplus
}
#endif

#endif /* C62_REMOTE_H */
