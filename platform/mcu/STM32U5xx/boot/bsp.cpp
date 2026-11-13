/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/***********************************************************************
 * bsp.cpp Part of the Miosix Embedded OS.
 * Board support package, this file initializes hardware.
 ************************************************************************/

#include <interfaces/platform.h>
#include <peripherals/gpio.h>
#include <interfaces/bsp.h>
#include <kernel/kernel.h>
#include <kernel/sync.h>
#include <hwconfig.h>

namespace miosix
{

// Iterations to wait for the voltage scaling ready flag, ~10ms at 4MHz
static constexpr uint32_t VOS_READY_TIMEOUT = 10000;

// Iterations to wait for the booster, the PLL and the clock switch
static constexpr uint32_t CLOCK_READY_TIMEOUT = 100000;

/*
 * PLL1: HSI16 input, 8-16MHz range, M = 1, N = 20 (VCO 320MHz), R = 2,
 * SYSCLK = 160MHz. The EPOD booster gets the PLL1 input clock undivided
 * (PLL1MBOOST = 1), within its 4-16MHz range.
 */
static constexpr uint32_t PLL1SRC_HSI16 = 2;
static constexpr uint32_t PLL1RGE_8_16 = 3;
static constexpr uint32_t PLL1_N = 20;
static constexpr uint32_t PLL1_R = 2;
static constexpr uint32_t SW_PLL1 = 3;
static constexpr uint32_t HPRE_DIV2 = 8;

static bool waitBits(volatile uint32_t *reg, uint32_t mask, uint32_t value)
{
    for (uint32_t i = 0; i < CLOCK_READY_TIMEOUT; i++) {
        if ((*reg & mask) == value)
            return true;
    }

    return false;
}

/**
 * Run the core at 160MHz from PLL1, following the sequence in RM0456, section
 * "EPOD booster": configure the PLL input and booster prescaler, enable the
 * booster, start the PLL, raise the flash latency, then switch SYSCLK with a
 * temporary AHB divide-by-two step.
 *
 * @return true on success; on failure the core keeps running from HSI16.
 */
static bool pllInit()
{
    RCC->PLL1CFGR = (PLL1SRC_HSI16 << RCC_PLL1CFGR_PLL1SRC_Pos)
                  | (PLL1RGE_8_16 << RCC_PLL1CFGR_PLL1RGE_Pos)
                  | RCC_PLL1CFGR_PLL1REN;

    PWR->VOSR |= PWR_VOSR_BOOSTEN;
    if (waitBits(&PWR->VOSR, PWR_VOSR_BOOSTRDY, PWR_VOSR_BOOSTRDY) == false)
        return false;

    RCC->PLL1DIVR = (RCC->PLL1DIVR & ~(RCC_PLL1DIVR_PLL1N | RCC_PLL1DIVR_PLL1R))
                  | ((PLL1_N - 1) << RCC_PLL1DIVR_PLL1N_Pos)
                  | ((PLL1_R - 1) << RCC_PLL1DIVR_PLL1R_Pos);

    RCC->CR |= RCC_CR_PLL1ON;
    if (waitBits(&RCC->CR, RCC_CR_PLL1RDY, RCC_CR_PLL1RDY) == false)
        return false;

    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_4WS;
    if (waitBits(&FLASH->ACR, FLASH_ACR_LATENCY, FLASH_ACR_LATENCY_4WS)
        == false)
        return false;

    RCC->CFGR2 = (RCC->CFGR2 & ~RCC_CFGR2_HPRE)
               | (HPRE_DIV2 << RCC_CFGR2_HPRE_Pos);
    RCC->CFGR1 = (RCC->CFGR1 & ~RCC_CFGR1_SW) | (SW_PLL1 << RCC_CFGR1_SW_Pos);
    bool ok = waitBits(&RCC->CFGR1, RCC_CFGR1_SWS,
                       SW_PLL1 << RCC_CFGR1_SWS_Pos);

    // Let the clock settle before removing the AHB divider
    for (volatile uint32_t i = 0; i < 100; i++)
        ;
    RCC->CFGR2 &= ~RCC_CFGR2_HPRE;

    return ok;
}

/**
 * Select voltage scaling range 1, run the core from the 16MHz HSI oscillator,
 * then from PLL1 at 160MHz.
 *
 * After reset the device runs from MSI at 4MHz in voltage range 4. Range 1 is
 * the full-performance range, needed by ADC1 and by the PLL frequencies used
 * later. One flash wait state is kept, valid for 16MHz in every range (RM0456,
 * table "Number of wait states according to CPU clock (HCLK) frequency").
 */
static void clockInit()
{
    // The core keeps running at 16MHz in any range, so a range change that
    // does not complete must not stop the boot.
    RCC->AHB3ENR |= RCC_AHB3ENR_PWREN;
    RCC_SYNC();
    PWR->VOSR = (PWR->VOSR & ~PWR_VOSR_VOS) | PWR_VOSR_VOS_1 | PWR_VOSR_VOS_0;
    for (uint32_t i = 0; i < VOS_READY_TIMEOUT; i++) {
        if ((PWR->VOSR & PWR_VOSR_VOSRDY) != 0)
            break;
    }

    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0)
        ;

    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_1WS;
    while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_1WS)
        ;

    RCC->CFGR1 = (RCC->CFGR1 & ~RCC_CFGR1_SW) | RCC_CFGR1_SW_0;
    while ((RCC->CFGR1 & RCC_CFGR1_SWS) != RCC_CFGR1_SWS_0)
        ;

    pllInit();
    SystemCoreClockUpdate();
}

//
// Initialization
//

void IRQbspInit()
{
    RCC->AHB2ENR1 |= RCC_AHB2ENR1_GPIOAEN | RCC_AHB2ENR1_GPIOBEN
                   | RCC_AHB2ENR1_GPIOCEN | RCC_AHB2ENR1_GPIODEN
                   | RCC_AHB2ENR1_GPIOEEN;
    RCC_SYNC();

    // Keep the transmitter powered down until the radio driver decides
    // otherwise. Drive the pin low before making it an output.
    gpio_clearPin(TX_PWR_EN);
    gpio_setMode(TX_PWR_EN, OUTPUT);

    clockInit();

    // Cycle counter, used for the busy-wait delays
    DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    SysTick->LOAD = SystemCoreClock / miosix::TICK_FREQ;
}

void bspInit2()
{
}

//
// Shutdown and reboot
//

void shutdown()
{
    reboot();
}

void reboot()
{
    disableInterrupts();
    miosix_private::IRQsystemReboot();
}

} // namespace miosix
