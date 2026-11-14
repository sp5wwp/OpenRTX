/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef I2C_BITBANG_H
#define I2C_BITBANG_H

#include <stdint.h>
#include "peripherals/i2c.h"
#include "peripherals/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configuration of an I2C bus driven by software on two GPIOs, both used as
 * open-drain outputs with external pull-ups.
 */
struct i2cBitbangCfg {
    struct gpioPin sda;    ///< Data line
    struct gpioPin scl;    ///< Clock line
    uint32_t halfPeriodUs; ///< Half of the clock period, in microseconds
};

extern const struct i2cApi i2cBitbang_driver;

/**
 * Define an I2C bus driven by software.
 *
 * @param name: name of the device.
 * @param cfg: pointer to the bus configuration (struct i2cBitbangCfg).
 * @param mutx: mutex for exclusive access, or NULL.
 */
#define I2C_BITBANG_DEVICE_DEFINE(name, cfg, mutx) \
    const struct i2cDevice name = {                \
        .driver = &i2cBitbang_driver,              \
        .periph = cfg,                             \
        .mutex = mutx,                             \
    };

/**
 * Configure the bus lines and leave the bus idle (both lines released).
 *
 * @param dev: I2C device.
 */
void i2cBitbang_init(const struct i2cDevice *dev);

/**
 * Release the bus lines (inputs).
 *
 * @param dev: I2C device.
 */
void i2cBitbang_terminate(const struct i2cDevice *dev);

#ifdef __cplusplus
}
#endif

#endif // I2C_BITBANG_H
