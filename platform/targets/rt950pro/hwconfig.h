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
#include "drivers/baseband/SI4732.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Screen: 240x320 ST7789V panel, portrait */
#define CONFIG_SCREEN_WIDTH 240
#define CONFIG_SCREEN_HEIGHT 320
#define CONFIG_PIX_FMT_RGB565

/*
 * Draw the screen 8 rows at a time instead of keeping a framebuffer, which
 * would not fit in the 96kB of SRAM
 */
#define CONFIG_GFX_BAND_ROWS 8

/* Device supports M17 mode */
#define CONFIG_M17

/*
 * Microphone: PA2 at 24kHz, see at32_adc.h. Its samples are sixteen to
 * an ADC count and the speech is a few tens of counts.
 */
#define CONFIG_MIC_OVERSAMPLE 3
#define CONFIG_MIC_GAIN 8

/* The radio's bands, in MHz. The radio driver has a path for each, in order */
#define CONFIG_RF_BANDS_MHZ \
    RF_BAND(136, 174), RF_BAND(222, 225), RF_BAND(400, 480)

/* The radio's three calibrated powers */
#define CONFIG_TX_POWER_STEPS_MW 1000, 5000, 10000

/* Codec2-mod's thread stack: its decoder takes 5kB, its encoder 1kB */
#define CODEC2_THREAD_STKSIZE 8192

/* Draw text with the anti-aliased fonts */
#define CONFIG_FONT_AA

/* Home screen shows three tuners */
#define CONFIG_UI_TUNERS 3

/* Keypad keys */
#define CONFIG_KBD_HAS_ARROWS
#define CONFIG_KBD_HAS_NUMERIC

/* Screen has adjustable brightness */
#define CONFIG_SCREEN_BRIGHTNESS

/* GPS and real time clock */
#define CONFIG_GPS
#define CONFIG_NMEA_RBUF_SIZE 128
#define CONFIG_RTC

/* Broadcast FM receiver */
//#define CONFIG_BROADCAST_FM

/* Bluetooth module on USART1 */
//#define CONFIG_BLUETOOTH

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
 * Audio path: speaker mute (high mutes), amplifier power, microphone mute
 * (high takes the microphone off its line), audio source selection (high for
 * the BK4829 chips, low for the MCU DAC) and Bluetooth switch (low routes the
 * audio to the speaker).
 */
extern const struct gpioPin rt950_spk_mute;
extern const struct gpioPin rt950_amp_pwr;
extern const struct gpioPin rt950_mic_mute;
extern const struct gpioPin rt950_audio_sw;
extern const struct gpioPin rt950_bt_sw;
extern const struct gpioPin rt950_bt_pwr;

/*
 * RF path switches. They are driven by the physical level: high enables the
 * path, low is idle.
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
extern const struct BK4829 rt950_bk4829_uhf_hf;
extern const struct spiDevice rt950_bk4829_bus;

/* SI4732 broadcast receiver, on a bit-banged I2C bus */
extern const struct i2cDevice rt950_si4732_i2c;
extern const struct SI4732 rt950_si4732;

#ifdef __cplusplus
}
#endif

#endif /* HWCONFIG_H */
