/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <drivers/ADC/adc_stm32.h>
#include <hwconfig.h>
#include <pthread.h>

static pthread_mutex_t adcMutex;

ADC_STM32_DEVICE_DEFINE(adc1, ADC1, &adcMutex, ADC_COUNTS_TO_UV(3300000, 14))

const struct wx4818n wx4818nA = {
    .clk = { WX_SCLK },
    .dio = { WX_SDIO },
    .cs  = { WX_CS_A },
};

const struct wx4818n wx4818nB = {
    .clk = { WX_SCLK },
    .dio = { WX_SDIO },
    .cs  = { WX_CS_B },
};
