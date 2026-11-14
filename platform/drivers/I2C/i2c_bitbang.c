/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include "interfaces/delays.h"
#include "drivers/I2C/i2c_bitbang.h"

/*
 * I2C master driven by software: both lines are open-drain outputs, a line
 * is released by writing it high and read back through its input register.
 * No clock stretching support.
 */

static inline const struct i2cBitbangCfg *cfgOf(const struct i2cDevice *dev)
{
    return (const struct i2cBitbangCfg *)dev->periph;
}

static void halfPeriod(const struct i2cBitbangCfg *cfg)
{
    delayUs(cfg->halfPeriodUs);
}

static void start(const struct i2cBitbangCfg *cfg)
{
    gpioPin_set(&cfg->sda);
    gpioPin_set(&cfg->scl);
    halfPeriod(cfg);
    gpioPin_clear(&cfg->sda);
    halfPeriod(cfg);
    gpioPin_clear(&cfg->scl);
    halfPeriod(cfg);
}

static void stop(const struct i2cBitbangCfg *cfg)
{
    gpioPin_clear(&cfg->sda);
    halfPeriod(cfg);
    gpioPin_set(&cfg->scl);
    halfPeriod(cfg);
    gpioPin_set(&cfg->sda);
    halfPeriod(cfg);
}

/* Clock one bit out, the data line is set while the clock is low */
static void writeBit(const struct i2cBitbangCfg *cfg, const bool bit)
{
    if (bit)
        gpioPin_set(&cfg->sda);
    else
        gpioPin_clear(&cfg->sda);

    halfPeriod(cfg);
    gpioPin_set(&cfg->scl);
    halfPeriod(cfg);
    gpioPin_clear(&cfg->scl);
}

static bool readBit(const struct i2cBitbangCfg *cfg)
{
    gpioPin_set(&cfg->sda);
    halfPeriod(cfg);
    gpioPin_set(&cfg->scl);
    halfPeriod(cfg);
    bool bit = gpioPin_read(&cfg->sda);
    gpioPin_clear(&cfg->scl);

    return bit;
}

/* Send one byte, returns true if the device acknowledged it */
static bool writeByte(const struct i2cBitbangCfg *cfg, const uint8_t byte)
{
    for (int i = 7; i >= 0; i--)
        writeBit(cfg, ((byte >> i) & 0x01) != 0);

    return readBit(cfg) == false;
}

static uint8_t readByte(const struct i2cBitbangCfg *cfg, const bool ack)
{
    uint8_t byte = 0;

    for (int i = 0; i < 8; i++)
        byte = (byte << 1) | (readBit(cfg) ? 1 : 0);

    writeBit(cfg, !ack);
    return byte;
}

static int i2cBitbang_write(const struct i2cDevice *dev, const uint8_t addr,
                            const void *data, const size_t len, const bool stp)
{
    const struct i2cBitbangCfg *cfg = cfgOf(dev);
    const uint8_t *ptr = (const uint8_t *)data;
    int ret = 0;

    start(cfg);
    if (writeByte(cfg, addr << 1) == false) {
        ret = -EIO;
        goto end;
    }

    for (size_t i = 0; i < len; i++) {
        if (writeByte(cfg, ptr[i]) == false) {
            ret = -EIO;
            goto end;
        }
    }

end:
    if (stp || (ret < 0))
        stop(cfg);

    return ret;
}

static int i2cBitbang_read(const struct i2cDevice *dev, const uint8_t addr,
                           void *data, const size_t len, const bool stp)
{
    const struct i2cBitbangCfg *cfg = cfgOf(dev);
    uint8_t *ptr = (uint8_t *)data;

    start(cfg);
    if (writeByte(cfg, (addr << 1) | 0x01) == false) {
        stop(cfg);
        return -EIO;
    }

    for (size_t i = 0; i < len; i++)
        ptr[i] = readByte(cfg, i < (len - 1));

    if (stp)
        stop(cfg);

    return 0;
}

const struct i2cApi i2cBitbang_driver = {
    .read = i2cBitbang_read,
    .write = i2cBitbang_write,
};

void i2cBitbang_init(const struct i2cDevice *dev)
{
    const struct i2cBitbangCfg *cfg = cfgOf(dev);

    gpioPin_set(&cfg->sda);
    gpioPin_set(&cfg->scl);
    gpioPin_setMode(&cfg->sda, OPEN_DRAIN);
    gpioPin_setMode(&cfg->scl, OPEN_DRAIN);
}

void i2cBitbang_terminate(const struct i2cDevice *dev)
{
    const struct i2cBitbangCfg *cfg = cfgOf(dev);

    gpioPin_setMode(&cfg->sda, INPUT);
    gpioPin_setMode(&cfg->scl, INPUT);
}
