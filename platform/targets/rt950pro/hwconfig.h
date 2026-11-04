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
#include "drivers/baseband/BK4829.h"

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

/* GPS and real time clock */
#define CONFIG_GPS
#define CONFIG_NMEA_RBUF_SIZE 128
#define CONFIG_RTC

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
extern const struct gpioPin rt950_gps_pwr;
extern const struct pwm_dt_spec rt950_lcd_bl_pwm;

/*
 * Audio path: speaker mute (high mutes), amplifier power and enable, audio
 * source selection (high for the BK4829 chips, low for the MCU DAC) and
 * Bluetooth switch (low routes the audio to the speaker).
 */
extern const struct gpioPin rt950_spk_mute;
extern const struct gpioPin rt950_amp_pwr;
extern const struct gpioPin rt950_amp_en;
extern const struct gpioPin rt950_audio_sw;
extern const struct gpioPin rt950_bt_sw;

/*
 * RF path switches. They are driven by the physical level: high is
 * deasserted, as in the vendor firmware.
 */
extern const struct gpioPin rt950_rf_u6r;
extern const struct gpioPin rt950_rf_u3r;
extern const struct gpioPin rt950_rf_v3r;
extern const struct gpioPin rt950_rf_u3t;
extern const struct gpioPin rt950_rf_v3t;
extern const struct gpioPin rt950_rf_sw3t;

/*
 * BK4829 transceivers sharing the same serial bus: the VHF one, wired to the
 * VHF LNA and PA, and the UHF one, wired to the UHF LNA and PA.
 */
extern const struct BK4829 rt950_bk4829_vhf;
extern const struct BK4829 rt950_bk4829_uhf;
extern const struct spiDevice rt950_bk4829_bus;

#ifdef __cplusplus
}
#endif

#endif /* HWCONFIG_H */
