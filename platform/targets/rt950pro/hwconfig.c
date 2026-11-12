/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <hwconfig.h>
#include "drivers/GPIO/gpio_zephyr.h"
#include "drivers/SPI/spi_zephyr.h"

#define USER_NODE DT_PATH(zephyr_user)

#define GPIO_PIN(port_, node_, prop_, idx_) \
    { .port = &(port_), .pin = DT_GPIO_PIN_BY_IDX(node_, prop_, idx_) }

BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(USER_NODE, pwr_switch_gpios),
                          DT_NODELABEL(gpioe)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(USER_NODE, pwr_enable_gpios),
                          DT_NODELABEL(gpioa)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(USER_NODE, kbd_bl_gpios),
                          DT_NODELABEL(gpiob)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(USER_NODE, led_red_gpios),
                          DT_NODELABEL(gpioc)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(USER_NODE, led_green_gpios),
                          DT_NODELABEL(gpioc)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(USER_NODE, ptt_gpios, 0),
                          DT_NODELABEL(gpioe)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(USER_NODE, ptt_gpios, 1),
                          DT_NODELABEL(gpioe)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(USER_NODE, vbat_gpios),
                          DT_NODELABEL(gpioa)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(USER_NODE, gps_pwr_gpios),
                          DT_NODELABEL(gpioa)));

BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(USER_NODE, rf_u6r_gpios),
                          DT_NODELABEL(gpioe)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(USER_NODE, rf_u3r_gpios),
                          DT_NODELABEL(gpioe)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(USER_NODE, rf_v3r_gpios),
                          DT_NODELABEL(gpiob)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(USER_NODE, rf_u3t_gpios),
                          DT_NODELABEL(gpioe)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(USER_NODE, rf_v3t_gpios),
                          DT_NODELABEL(gpiob)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(USER_NODE, rf_sw3t_gpios),
                          DT_NODELABEL(gpioe)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(USER_NODE, bk4829_scn_gpios, 0),
                          DT_NODELABEL(gpioe)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(USER_NODE, bk4829_scn_gpios, 1),
                          DT_NODELABEL(gpioe)));

const struct gpioPin rt950_pwr_switch = GPIO_PIN(GpioE, USER_NODE,
                                                 pwr_switch_gpios, 0);
const struct gpioPin rt950_power_enable = GPIO_PIN(GpioA, USER_NODE,
                                                   pwr_enable_gpios, 0);
const struct gpioPin rt950_kbd_bl = GPIO_PIN(GpioB, USER_NODE, kbd_bl_gpios, 0);
const struct gpioPin rt950_led_red = GPIO_PIN(GpioC, USER_NODE, led_red_gpios,
                                              0);
const struct gpioPin rt950_led_green = GPIO_PIN(GpioC, USER_NODE,
                                                led_green_gpios, 0);
const struct gpioPin rt950_ptt = GPIO_PIN(GpioE, USER_NODE, ptt_gpios, 0);
const struct gpioPin rt950_ptt2 = GPIO_PIN(GpioE, USER_NODE, ptt_gpios, 1);
const struct gpioPin rt950_vbat = GPIO_PIN(GpioA, USER_NODE, vbat_gpios, 0);
const struct gpioPin rt950_gps_pwr = GPIO_PIN(GpioA, USER_NODE, gps_pwr_gpios,
                                              0);

const struct gpioPin rt950_rf_u6r = GPIO_PIN(GpioE, USER_NODE, rf_u6r_gpios, 0);
const struct gpioPin rt950_rf_u3r = GPIO_PIN(GpioE, USER_NODE, rf_u3r_gpios, 0);
const struct gpioPin rt950_rf_v3r = GPIO_PIN(GpioB, USER_NODE, rf_v3r_gpios, 0);
const struct gpioPin rt950_rf_u3t = GPIO_PIN(GpioE, USER_NODE, rf_u3t_gpios, 0);
const struct gpioPin rt950_rf_v3t = GPIO_PIN(GpioB, USER_NODE, rf_v3t_gpios, 0);
const struct gpioPin rt950_rf_sw3t = GPIO_PIN(GpioE, USER_NODE, rf_sw3t_gpios,
                                              0);

const struct pwm_dt_spec rt950_lcd_bl_pwm =
    PWM_DT_SPEC_GET(DT_NODELABEL(lcd_bl_pwm));

/*
 * BK4829 serial bus: SCLK PE10, SDATA PE11 (half duplex), shared by both
 * chips. The mutex serializes the accesses to the two chips.
 */
#define BK4829_BUS DEVICE_DT_GET(DT_NODELABEL(bk4829_spi))
/* Keep 5us clock half periods for the chip's slow data output rise time. */
#define BK4829_BUS_FREQUENCY 100000

static pthread_mutex_t rt950_bk4829_mutex;
static const struct spiZephyrCfg rt950_bk4829_spiCfg = {
    .bus = BK4829_BUS,
    .frequency = BK4829_BUS_FREQUENCY,
    .flags = SPI_FLAG_HALF_DUPLEX,
};

SPI_ZEPHYR_DEVICE_DEFINE(rt950_bk4829_bus, rt950_bk4829_spiCfg,
                         &rt950_bk4829_mutex)

const struct BK4829 rt950_bk4829_vhf = {
    .bus = &rt950_bk4829_bus,
    .scn = GPIO_PIN(GpioE, USER_NODE, bk4829_scn_gpios, 0),
};

const struct BK4829 rt950_bk4829_uhf = {
    .bus = &rt950_bk4829_bus,
    .scn = GPIO_PIN(GpioE, USER_NODE, bk4829_scn_gpios, 1),
};
