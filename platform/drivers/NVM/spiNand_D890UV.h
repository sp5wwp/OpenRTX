/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SPINAND_D890UV_H
#define SPINAND_D890UV_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Read-only driver for the SPI NAND flash of the AnyTone AT-D890UV, connected
 * to GPIOs and driven by software. Command set and addressing (4kB pages)
 * from the stock firmware V1.05, function at 0x08037B18.
 */

/**
 * Configure the bus lines.
 */
void spiNand_init();

/**
 * Read the JEDEC manufacturer and device ID.
 *
 * @param id: buffer for the three ID bytes.
 */
void spiNand_readId(uint8_t *id);

/**
 * Read data, without crossing a page boundary.
 *
 * @param addr: byte address, as used by the stock firmware.
 * @param buf: destination buffer.
 * @param len: number of bytes.
 * @return status register value after the page load, or a negative value
 * on timeout.
 */
int spiNand_read(uint32_t addr, void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif // SPINAND_D890UV_H
