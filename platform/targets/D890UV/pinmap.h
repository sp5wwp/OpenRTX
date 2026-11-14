/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PINMAP_H
#define PINMAP_H

#include <stm32u5xx.h>
#include "drivers/GPIO/gpio_stm32.h"

/*
 * Pin assignment recovered from the HAL_GPIO_Init() calls of the stock
 * AnyTone D890UV firmware V1.05 (D890UV_V1.05_20260521), cross-checked with
 * the "D890UVS I/O assignment" table of the AnyTone D890UV M17 design note.
 */

/* Display: ST7735S, 8080 8-bit parallel bus, bit-banged */
#define LCD_D0 GPIOE, 7
#define LCD_D1 GPIOE, 8
#define LCD_D2 GPIOE, 9
#define LCD_D3 GPIOE, 10
#define LCD_D4 GPIOE, 11
#define LCD_D5 GPIOE, 12
#define LCD_D6 GPIOE, 13
#define LCD_D7 GPIOE, 14
#define LCD_DC GPIOD, 12
#define LCD_RST GPIOD, 13
#define LCD_CS GPIOD, 14
#define LCD_WR GPIOD, 15
#define LCD_BKLIGHT GPIOB, 0

/* Analog inputs */
#define AIN_VBAT GPIOC, 1   // Battery voltage divider
#define AIN_VOLUME GPIOC, 2 // Volume potentiometer
#define AIN_AF GPIOC, 0     // "TONE DET A/D": band A or band B audio output
#define AIN_MIC GPIOA, 7    // "A/D DET FOR VOX": microphone amplifier

/* External SPI NAND flash, bit-banged by the stock firmware */
#define NAND_CS GPIOC, 5
#define NAND_CLK GPIOE, 15
#define NAND_MOSI GPIOB, 10
#define NAND_MISO GPIOB, 8

/* Audio section */
#define UHF_FILTER GPIOA, 5 // DAC1 channel 2, UHF front-end filter tuning
#define APC_DAC GPIOA, 4    // DAC1 channel 1, transmitter power control
#define SPK_EN GPIOC, 12    // LN4898 speaker amplifier, high enables
#define AF_SW0 GPIOD, 0     // Audio routing switch ("MOD REPEAT")
#define AF_SW1 GPIOD, 1     // Audio routing switch ("MOD AF-RE...")
#define AF_SW2 GPIOD, 2     // Audio routing switch ("MOD SEC H...")
#define MIC_SEL GPIOE, 0    // Mic source: low radio mic (MICI), high MICO
#define BT_PWR GPIOA, 1     // Bluetooth module power
#define APC_PWR GPIOE, 1    // Transmitter APC power

/* LEDs, high on (stock: 0x081140EC callers and the PTT handler) */
#define LED_DIGI_RX GPIOE, 3 // "DIGI-RX LED"
#define LED_ANL_RX GPIOE, 4  // "ANL-RX LED", green
#define LED_TX GPIOE, 5      // "TX LED", red

/*
 * Digital board, SCT3288 baseband (stock GPIO setup 0x0806235C, 0x08061770,
 * 0x080617C4). SPI2 on PB13-PB15, alternate function 5.
 */
#define SCT_SCK GPIOB, 13
#define SCT_MISO GPIOB, 14
#define SCT_MOSI GPIOB, 15
#define SCT_CS &GpioB, 12   // Chip select, active low
#define SCT_PWR &GpioC, 6   // Board power, high on
#define SCT_RST &GpioD, 8   // Reset, active low
#define SCT_CTL1 &GpioD, 9  // Held low by stock
#define SCT_CTL2 &GpioD, 10 // Held low by stock
#define SCT_RDY &GpioD, 11  // High when the chip has data
#define SCT_FRAME &GpioD, 7 // High during a frame from the chip

/*
 * Bit-banged I2C bus (stock 0x080584D4): audio volume device, charger and
 * accelerometer. SDA and SCL are swapped against the I2C1 pins.
 */
#define I2C_SDA &GpioB, 6
#define I2C_SCL &GpioB, 7

/* Power */
#define PWR_ON_DET GPIOA, 0 // Power switch status, polarity see platform.c
#define PWR_HOLD GPIOC, 3   // ORed with the power switch, high holds on

/* RF section */
#define TX_PWR_EN GPIOA, 15 // Transmitter power, high enables
#define WX_SCLK &GpioB, 3   // WX4818N serial clock, shared
#define WX_SDIO &GpioB, 4   // WX4818N serial data, shared
#define WX_CS_A &GpioB, 5   // WX4818N band A chip select
#define WX_CS_B &GpioD, 3   // WX4818N band B chip select

/* Keypad rows, active low. Columns are the display data lines LCD_D0-D7 */
#define KBD_ROW1 GPIOC, 4
#define KBD_ROW2 GPIOB, 1
#define KBD_ROW3 GPIOB, 2

/* Push-to-talk and side keys, all active low */
#define PTT_SW GPIOA, 10
#define SIDE_KEY1 GPIOE, 2  // Upper side key, PF1
#define SIDE_KEY2 GPIOA, 14 // Lower side key, PF2
#define ALARM_KEY GPIOA, 8  // Top key, PF3

/* Channel knob, quadrature encoder */
#define KNOB_A GPIOC, 8
#define KNOB_B GPIOC, 9

#endif // PINMAP_H
