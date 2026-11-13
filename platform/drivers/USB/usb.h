/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef USB_H
#define USB_H

#include <stdint.h>
#include <unistd.h>
#include "tusb.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise the USB driver.
 */
void usb_init();

/**
 * Shut down the USB driver.
 */
void usb_terminate();

/**
 * Send data over the USB serial port, waiting for buffer space. Not available
 * on all platforms.
 *
 * @param data: data to send.
 * @param len: number of bytes.
 */
void usb_write(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* USB_H */
