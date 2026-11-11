/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef BLUETOOTH_RT950PRO_H
#define BLUETOOTH_RT950PRO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Driver for the Bluetooth module of the Radtel RT-950 Pro, a BLE module acting
 * as a transparent serial link connected to a Zephyr UART: every byte written
 * is sent to the remote device and every byte received from it is available for
 * reading. The module is configured over the air by the remote device, no
 * commands are sent by this driver.
 *
 * Received bytes are buffered by the UART RX interrupt. Only a single module is
 * supported.
 */

/**
 * Power on the module, start receiving and start the monitor, which logs the
 * received bytes and echoes them back to the remote device.
 *
 * The module is enabled by PB9 and its UART, the "bluetooth" alias of the
 * devicetree, runs at 9600 baud. The function waits for the module to start.
 *
 * @return zero on success, a negative error code otherwise.
 */
int bluetooth_init(void);

/**
 * Stop receiving and power off the module.
 */
void bluetooth_terminate(void);

/**
 * Send data to the module.
 *
 * @param data: data to be sent.
 * @param len: number of bytes to send.
 * @return number of bytes sent, or a negative error code.
 */
int bluetooth_write(const void *data, size_t len);

/**
 * Read the received data, without blocking.
 *
 * @param buf: buffer for the received data.
 * @param len: size of the buffer.
 * @return number of bytes read, zero if no data is available, or a negative
 * error code.
 */
int bluetooth_read(void *buf, size_t len);

/**
 * Discard all the received data not read yet.
 */
void bluetooth_flush(void);

/**
 * Get the number of received bytes discarded because the receive buffer was
 * full.
 *
 * @return number of discarded bytes since the initialisation.
 */
uint32_t bluetooth_getOverflows(void);

/**
 * Check if a remote device is likely connected.
 *
 * The module has no connection state output: a connection is assumed when data
 * has been received during the last few seconds.
 *
 * @return true if a remote device is likely connected.
 */
bool bluetooth_isConnected(void);

#ifdef __cplusplus
}
#endif

#endif /* BLUETOOTH_RT950PRO_H */
