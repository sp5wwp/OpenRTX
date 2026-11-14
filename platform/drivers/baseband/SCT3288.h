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

/**
 * Packet types (TYPE byte of the Sicomm packet interface): bits 3:0 select
 * the protocol, bit 5 makes a read request, bit 6 bypasses to the codec.
 */
enum sct3288PacketType {
    SCT3288_PKT_CONTROL = 0x00, ///< Control packet
    SCT3288_PKT_DPMR = 0x03,    ///< DPMR packet, also used by stock for reads
    SCT3288_PKT_DMR = 0x05,     ///< DMR packet, also used to start analog calls
    SCT3288_PKT_READ = 0x20,    ///< Read request flag
    SCT3288_PKT_CODEC = 0x40,   ///< Codec register write
};

/**
 * Send one packet: start bytes, length, type and fields, padded to an even
 * length as the chip expects.
 *
 * @param dev: device.
 * @param type: packet type.
 * @param fields: field identifier followed by its data.
 * @param len: length of fields, at most 32 bytes.
 * @return 0 on success, -EINVAL if the packet is too long.
 */
int sct3288_send(const struct sct3288 *dev, uint8_t type, const uint8_t *fields,
                 size_t len);

/**
 * Send one packet and wait for the chip to answer it, either with the same
 * field or with ACK_MESSAGE (field 0x17). Other packets arriving meanwhile
 * are dropped.
 *
 * @param dev: device.
 * @param type: packet type.
 * @param fields: field identifier followed by its data.
 * @param len: length of fields.
 * @param timeoutMs: time to wait for the answer.
 * @return 0 when the chip answered without error, -EIO when it reported an
 * error, -ETIMEDOUT without answer.
 */
int sct3288_command(const struct sct3288 *dev, uint8_t type,
                    const uint8_t *fields, size_t len, uint32_t timeoutMs);

/**
 * Wait for the chip to report ACK_MESSAGE with the given code.
 *
 * @param dev: device.
 * @param code: expected code (1: ready after a reset).
 * @param timeoutMs: time to wait.
 * @return 0 when received, -ETIMEDOUT otherwise.
 */
int sct3288_waitAck(const struct sct3288 *dev, uint8_t code,
                    uint32_t timeoutMs);

/**
 * Read the chip's firmware version (Q_SW_VER, field 0x32) and keep it for
 * sct3288_getFwVersion().
 *
 * @param dev: device.
 * @return 0 on success, -ETIMEDOUT without answer.
 */
int sct3288_readFwVersion(const struct sct3288 *dev);

/**
 * Firmware version read by sct3288_readFwVersion(), e.g. "V3.01.01A6".
 *
 * @return the version, or an empty string if it was not read.
 */
const char *sct3288_getFwVersion();

/**
 * Read and drop whatever the chip has to send. The chip must be read
 * regularly: unread packets make it repeat them.
 *
 * @param dev: device.
 */
void sct3288_drain(const struct sct3288 *dev);

#ifdef __cplusplus
}
#endif

#endif // SCT3288_H
