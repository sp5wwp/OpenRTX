/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef GPS_STM32U5_H
#define GPS_STM32U5_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Set up the USART selected with CONFIG_GPS_STM32U5_USARTn for an NMEA
 * receiver: receive only, 8N1. The pins are set up by the caller.
 *
 * @param baud: baud rate.
 */
void gpsStm32u5_init(const uint32_t baud);

/**
 * Stop the USART and its clock.
 */
void gpsStm32u5_terminate();

/**
 * Start receiving.
 *
 * @param priv: unused.
 */
void gpsStm32u5_enable(void *priv);

/**
 * Stop receiving.
 *
 * @param priv: unused.
 */
void gpsStm32u5_disable(void *priv);

/**
 * Get the oldest complete NMEA sentence received.
 *
 * @param priv: unused.
 * @param buf: destination buffer.
 * @param maxLength: size of the buffer.
 * @return the sentence length, 0 if none is available, -1 if it does not fit.
 */
int gpsStm32u5_getNmeaSentence(void *priv, char *buf, const size_t maxLength);

#ifdef __cplusplus
}
#endif

#endif // GPS_STM32U5_H
