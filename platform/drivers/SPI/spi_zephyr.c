/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "spi_zephyr.h"
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spi_zephyr, LOG_LEVEL_INF);

int spiZephyr_transfer(const struct spiDevice *dev, const void *txBuf,
                       void *rxBuf, const size_t size)
{
    const struct spiZephyrCfg *cfg = (const struct spiZephyrCfg *)dev->priv;
    const struct spi_config spiCfg = {
        .frequency = cfg->frequency,
        .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8)
                   | ((cfg->flags & SPI_FLAG_HALF_DUPLEX) ? SPI_HALF_DUPLEX :
                                                            0),
    };
    struct spi_buf txSpiBuf = { .buf = (void *)txBuf, .len = size };
    struct spi_buf_set txBufSet = { .buffers = &txSpiBuf, .count = 1 };
    struct spi_buf rxSpiBuf = { .buf = rxBuf, .len = size };
    struct spi_buf_set rxBufSet = { .buffers = &rxSpiBuf, .count = 1 };

    int ret = spi_transceive(cfg->bus, &spiCfg,
                             (txBuf != NULL) ? &txBufSet : NULL,
                             (rxBuf != NULL) ? &rxBufSet : NULL);
    if (ret < 0)
        LOG_ERR("SPI transfer failed (%d)", ret);
    return (ret < 0) ? ret : 0;
}

int spiZephyr_init(const struct spiDevice *dev)
{
    const struct spiZephyrCfg *cfg = (const struct spiZephyrCfg *)dev->priv;

    spiDevice_init(dev);

    return device_is_ready(cfg->bus) ? 0 : -ENODEV;
}

void spiZephyr_terminate(const struct spiDevice *dev)
{
    spiDevice_terminate(dev);
}
