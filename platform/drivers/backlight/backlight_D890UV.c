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
 * Backlight PWM on PB0, TIM3 channel 3 (AF2), set up as the stock firmware
 * does (0x08062E40): prescaler 3, period 4095. With the 160MHz timer clock
 * this gives about 9.8kHz.
 *
 * The stock brightness levels 1-5 use a duty of 2550, 2750, 2850, 2950 and
 * 4095 counts (table at 0x0814A90A); the backlight timeout writes 0. The
 * OpenRTX level, 0-100, is mapped linearly on the same span: 0 is off, 1 is
 * the stock minimum and 100 the stock maximum.
 */

static const uint16_t PWM_PERIOD = 4095;
static const uint16_t PWM_MIN = 2550;

void backlight_init()
{
    gpio_clearPin(LCD_BKLIGHT);
    gpio_setMode(LCD_BKLIGHT, ALTERNATE | ALTERNATE_FUNC(2));

    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM3EN;
    __DSB();

    TIM3->CR1 = TIM_CR1_ARPE;
    TIM3->PSC = 3;
    TIM3->ARR = PWM_PERIOD;
    TIM3->CNT = 0;
    TIM3->CCMR2 = TIM_CCMR2_OC3M_2 // PWM mode 1
                | TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3PE;
    TIM3->CCR3 = 0;
    TIM3->CCER = TIM_CCER_CC3E;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->CR1 |= TIM_CR1_CEN;
}

void backlight_terminate()
{
    TIM3->CCR3 = 0;
    TIM3->CR1 &= ~TIM_CR1_CEN;
    gpio_setMode(LCD_BKLIGHT, OUTPUT);
    gpio_clearPin(LCD_BKLIGHT);

    RCC->APB1ENR1 &= ~RCC_APB1ENR1_TIM3EN;
    __DSB();
}

/*
 * This function is defined in display.h
 */
void display_setBacklightLevel(uint8_t level)
{
    if (level > 100)
        level = 100;

    uint32_t duty = 0;
    if (level > 0)
        duty = PWM_MIN + ((PWM_PERIOD - PWM_MIN) * (level - 1)) / 99;

    TIM3->CCR3 = duty;
}
