/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef GPIO_ZEPHYR_H
#define GPIO_ZEPHYR_H

#include "peripherals/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * OpenRTX gpioDev instances backed by Zephyr GPIO controllers.
 *
 * These allow generic OpenRTX drivers, based on the gpioPin API, to run on
 * Zephyr targets such as the Retevis C62 and Radtel RT-950 Pro, where GPIOs
 * are owned by the Zephyr device model.
 *
 * Pin numbers match the SoC GPIO numbering, e.g. ``{ &GpioA, 13 }`` is
 * GPIOA pin 13. They must be kept in sync with the board devicetree.
 * GpioC, GpioD and GpioE are defined only for enabled devicetree controllers.
 */
extern const struct gpioDev GpioA;
extern const struct gpioDev GpioB;
extern const struct gpioDev GpioC;
extern const struct gpioDev GpioD;
extern const struct gpioDev GpioE;

#ifdef __cplusplus
}
#endif

#endif /* GPIO_ZEPHYR_H */
