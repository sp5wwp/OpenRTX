/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HWCONFIG_H
#define HWCONFIG_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <peripherals/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The panel is a 240x320 ST7789V. To fit the 96kB of SRAM, OpenRTX draws a
 * 160x128 RGB565 frame, which is scaled by 1.5 and shown centered on the
 * panel.
 */
#define CONFIG_SCREEN_WIDTH 160
#define CONFIG_SCREEN_HEIGHT 128
#define CONFIG_PIX_FMT_RGB565

/* Screen has adjustable brightness */
#define CONFIG_SCREEN_BRIGHTNESS

/* Battery type */
#define CONFIG_BAT_LIION
#define CONFIG_BAT_NCELLS 2


/* Battery voltage measurement */
#define RT950_VBAT_SCALE_NUM 482
#define RT950_VBAT_SCALE_DEN 160

extern const struct gpioPin rt950_vbat;
extern const struct gpioPin rt950_pwr_switch;
extern const struct gpioPin rt950_power_enable;
extern const struct gpioPin rt950_kbd_bl;
extern const struct gpioPin rt950_led_red;
extern const struct gpioPin rt950_led_green;
extern const struct gpioPin rt950_ptt;
extern const struct gpioPin rt950_ptt2;
extern const struct pwm_dt_spec rt950_lcd_bl_pwm;

#ifdef __cplusplus
}
#endif

#endif /* HWCONFIG_H */
