/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <hwconfig.h>
#include <interfaces/display.h>
#include <peripherals/gpio.h>
#include "backlight.h"

/*
 * The stock firmware drives the backlight pin as a plain output, so brightness
 * control is limited to on/off for now.
 */

void backlight_init()
{
    gpio_clearPin(LCD_BKLIGHT);
    gpio_setMode(LCD_BKLIGHT, OUTPUT);
}

void backlight_terminate()
{
    gpio_clearPin(LCD_BKLIGHT);
}

/*
 * This function is defined in display.h
 */
void display_setBacklightLevel(uint8_t level)
{
    if (level > 0)
        gpio_setPin(LCD_BKLIGHT);
    else
        gpio_clearPin(LCD_BKLIGHT);
}
