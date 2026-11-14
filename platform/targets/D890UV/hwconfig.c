/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <drivers/ADC/adc_stm32.h>
#include <hwconfig.h>
#include <pthread.h>
#include "drivers/SPI/spi_stm32.h"
#include "drivers/I2C/i2c_bitbang.h"

static pthread_mutex_t adcMutex;

ADC_STM32_DEVICE_DEFINE(adc1, ADC1, &adcMutex, ADC_COUNTS_TO_UV(3300000, 14))

const struct wx4818n wx4818nA = {
    .clk = { WX_SCLK },
    .dio = { WX_SDIO },
    .cs = { WX_CS_A },
};

const struct wx4818n wx4818nB = {
    .clk = { WX_SCLK },
    .dio = { WX_SDIO },
    .cs = { WX_CS_B },
};

/* Bit-banged I2C bus, about 100kHz */
static const struct i2cBitbangCfg i2cCfg = {
    .sda = { I2C_SDA },
    .scl = { I2C_SCL },
    .halfPeriodUs = 5,
};

static pthread_mutex_t i2cMutex = PTHREAD_MUTEX_INITIALIZER;

I2C_BITBANG_DEVICE_DEFINE(i2c_bus, &i2cCfg, &i2cMutex)

/* SCT3288 link: SPI2, as stock (0x0806207C) */
SPI_STM32_DEVICE_DEFINE(sct_spi, SPI2, NULL)

const struct sct3288 sct3288 = {
    .spi = &sct_spi,
    .cs = { SCT_CS },
    .power = { SCT_PWR },
    .reset = { SCT_RST },
    .ctl1 = { SCT_CTL1 },
    .ctl2 = { SCT_CTL2 },
    .ready = { SCT_RDY },
    .frame = { SCT_FRAME },
};
