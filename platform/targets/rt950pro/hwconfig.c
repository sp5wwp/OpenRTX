/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <hwconfig.h>
#include "drivers/GPIO/gpio_zephyr.h"

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

const struct pwm_dt_spec rt950_lcd_bl_pwm =
    PWM_DT_SPEC_GET(DT_NODELABEL(lcd_bl_pwm));
