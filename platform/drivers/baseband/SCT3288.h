/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SCT3288_H
#define SCT3288_H

#include <stdint.h>
#include <stddef.h>
#include "peripherals/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

struct spiDevice;

/**
 * Sicomm SCT3288 baseband processor, packet interface over SPI.
 */
struct sct3288 {
    const struct spiDevice *spi; ///< SPI bus, mode 0, MSB first
    struct gpioPin cs;           ///< Chip select, active low
    struct gpioPin power;        ///< Power enable, active high
    struct gpioPin reset;        ///< Reset, active low
    struct gpioPin ctl1;         ///< Control line held low by the host
    struct gpioPin ctl2;         ///< Control line held low by the host
    struct gpioPin ready;        ///< High when the chip has data to send
    struct gpioPin frame;        ///< High during a frame from the chip
};

/**
 * Power the chip up and reset it, then wait for its boot status
 * (ACK_MESSAGE = 0x0E).
 *
 * @param dev: device.
 * @return 0 when the boot status arrived, -ETIMEDOUT otherwise.
 */
int sct3288_init(const struct sct3288 *dev);

/**
 * Power the chip down.
 *
 * @param dev: device.
 */
void sct3288_terminate(const struct sct3288 *dev);

/**
 * Send bytes to the chip, in a single chip select cycle.
 *
 * @param dev: device.
 * @param data: bytes to send.
 * @param len: number of bytes.
 */
void sct3288_write(const struct sct3288 *dev, const void *data, size_t len);

/**
 * Read the bytes the chip has ready, while its ready line is high.
 *
 * @param dev: device.
 * @param buf: destination.
 * @param maxLen: buffer size.
 * @return number of bytes read.
 */
size_t sct3288_read(const struct sct3288 *dev, void *buf, size_t maxLen);

#ifdef __cplusplus
}
#endif

#endif // SCT3288_H
