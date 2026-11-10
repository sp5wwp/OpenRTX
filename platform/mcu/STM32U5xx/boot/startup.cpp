/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * C++ startup code for STM32U5 devices, based on the STM32F4 and AT32F423
 * startup files (Terraneo Federico, based on ST startup code), modified to
 * boot Miosix. Supports interrupt handlers in C++ without extern "C".
 *
 * The interrupt vector list is taken from the IRQn_Type enumeration in
 * stm32u575xx.h (STMicroelectronics cmsis-device-u5).
 */

#include "interfaces/arch_registers.h"
#include "kernel/stage_2_boot.h"
#include "core/interrupts.h" // For the unexpected interrupt call
#include <string.h>

/**
 * Called by Reset_Handler, performs initialization and calls main.
 * Never returns.
 */
void program_startup() __attribute__((noreturn));
void program_startup()
{
    __disable_irq();

    // SystemInit() is called before initializing .data and zeroing .bss: it
    // does not touch global variables. It also relocates the vector table to
    // FLASH_BASE + VECT_TAB_OFFSET, the address the bootloader starts us at.
    SystemInit();

    // These are defined in the linker script
    extern unsigned char _etext asm("_etext");
    extern unsigned char _data asm("_data");
    extern unsigned char _edata asm("_edata");
    extern unsigned char _bss_start asm("_bss_start");
    extern unsigned char _bss_end asm("_bss_end");

    // Initialize .data section, clear .bss section
    unsigned char *etext = &_etext;
    unsigned char *data = &_data;
    unsigned char *edata = &_edata;
    unsigned char *bss_start = &_bss_start;
    unsigned char *bss_end = &_bss_end;
    memcpy(data, etext, edata - data);
    memset(bss_start, 0, bss_end - bss_start);

    // Move on to stage 2
    _init();

    // If main returns, reboot
    NVIC_SystemReset();
    for (;;)
        ;
}

/**
 * Reset handler, called by hardware immediately after reset
 */
void Reset_Handler() __attribute__((__interrupt__, noreturn));
void Reset_Handler()
{
    /*
     * Initialize process stack and switch to it.
     * This is required for booting Miosix, a small portion of the top of the
     * heap area will be used as stack until the first thread starts. After,
     * this stack will be abandoned and the process stack will point to the
     * current thread's stack.
     */
    asm volatile("ldr r0,  =_heap_end          \n\t"
                 "msr psp, r0                  \n\t"
                 "movw r0, #2                  \n\n" // Privileged, process stack
                 "msr control, r0              \n\t"
                 "isb                          \n\t" ::: "r0");

    program_startup();
}

/**
 * All unused interrupts call this function.
 */
extern "C" void Default_Handler()
{
    unexpectedInterrupt();
}

// System handlers, not weak because they are surely defined by Miosix
void Reset_Handler();
void NMI_Handler();
void HardFault_Handler();
void MemManage_Handler();
void BusFault_Handler();
void UsageFault_Handler();
void SVC_Handler();
void DebugMon_Handler();
void PendSV_Handler();
void SysTick_Handler();

// Interrupt handlers
void __attribute__((weak)) WWDG_IRQHandler();
void __attribute__((weak)) PVD_PVM_IRQHandler();
void __attribute__((weak)) RTC_IRQHandler();
void __attribute__((weak)) RTC_S_IRQHandler();
void __attribute__((weak)) TAMP_IRQHandler();
void __attribute__((weak)) RAMCFG_IRQHandler();
void __attribute__((weak)) FLASH_IRQHandler();
void __attribute__((weak)) FLASH_S_IRQHandler();
void __attribute__((weak)) GTZC_IRQHandler();
void __attribute__((weak)) RCC_IRQHandler();
void __attribute__((weak)) RCC_S_IRQHandler();
void __attribute__((weak)) EXTI0_IRQHandler();
void __attribute__((weak)) EXTI1_IRQHandler();
void __attribute__((weak)) EXTI2_IRQHandler();
void __attribute__((weak)) EXTI3_IRQHandler();
void __attribute__((weak)) EXTI4_IRQHandler();
void __attribute__((weak)) EXTI5_IRQHandler();
void __attribute__((weak)) EXTI6_IRQHandler();
void __attribute__((weak)) EXTI7_IRQHandler();
void __attribute__((weak)) EXTI8_IRQHandler();
void __attribute__((weak)) EXTI9_IRQHandler();
void __attribute__((weak)) EXTI10_IRQHandler();
void __attribute__((weak)) EXTI11_IRQHandler();
void __attribute__((weak)) EXTI12_IRQHandler();
void __attribute__((weak)) EXTI13_IRQHandler();
void __attribute__((weak)) EXTI14_IRQHandler();
void __attribute__((weak)) EXTI15_IRQHandler();
void __attribute__((weak)) IWDG_IRQHandler();
void __attribute__((weak)) GPDMA1_Channel0_IRQHandler();
void __attribute__((weak)) GPDMA1_Channel1_IRQHandler();
void __attribute__((weak)) GPDMA1_Channel2_IRQHandler();
void __attribute__((weak)) GPDMA1_Channel3_IRQHandler();
void __attribute__((weak)) GPDMA1_Channel4_IRQHandler();
void __attribute__((weak)) GPDMA1_Channel5_IRQHandler();
void __attribute__((weak)) GPDMA1_Channel6_IRQHandler();
void __attribute__((weak)) GPDMA1_Channel7_IRQHandler();
void __attribute__((weak)) ADC1_IRQHandler();
void __attribute__((weak)) DAC1_IRQHandler();
void __attribute__((weak)) FDCAN1_IT0_IRQHandler();
void __attribute__((weak)) FDCAN1_IT1_IRQHandler();
void __attribute__((weak)) TIM1_BRK_IRQHandler();
void __attribute__((weak)) TIM1_UP_IRQHandler();
void __attribute__((weak)) TIM1_TRG_COM_IRQHandler();
void __attribute__((weak)) TIM1_CC_IRQHandler();
void __attribute__((weak)) TIM2_IRQHandler();
void __attribute__((weak)) TIM3_IRQHandler();
void __attribute__((weak)) TIM4_IRQHandler();
void __attribute__((weak)) TIM5_IRQHandler();
void __attribute__((weak)) TIM6_IRQHandler();
void __attribute__((weak)) TIM7_IRQHandler();
void __attribute__((weak)) TIM8_BRK_IRQHandler();
void __attribute__((weak)) TIM8_UP_IRQHandler();
void __attribute__((weak)) TIM8_TRG_COM_IRQHandler();
void __attribute__((weak)) TIM8_CC_IRQHandler();
void __attribute__((weak)) I2C1_EV_IRQHandler();
void __attribute__((weak)) I2C1_ER_IRQHandler();
void __attribute__((weak)) I2C2_EV_IRQHandler();
void __attribute__((weak)) I2C2_ER_IRQHandler();
void __attribute__((weak)) SPI1_IRQHandler();
void __attribute__((weak)) SPI2_IRQHandler();
void __attribute__((weak)) USART1_IRQHandler();
void __attribute__((weak)) USART2_IRQHandler();
void __attribute__((weak)) USART3_IRQHandler();
void __attribute__((weak)) UART4_IRQHandler();
void __attribute__((weak)) UART5_IRQHandler();
void __attribute__((weak)) LPUART1_IRQHandler();
void __attribute__((weak)) LPTIM1_IRQHandler();
void __attribute__((weak)) LPTIM2_IRQHandler();
void __attribute__((weak)) TIM15_IRQHandler();
void __attribute__((weak)) TIM16_IRQHandler();
void __attribute__((weak)) TIM17_IRQHandler();
void __attribute__((weak)) COMP_IRQHandler();
void __attribute__((weak)) OTG_FS_IRQHandler();
void __attribute__((weak)) CRS_IRQHandler();
void __attribute__((weak)) FMC_IRQHandler();
void __attribute__((weak)) OCTOSPI1_IRQHandler();
void __attribute__((weak)) PWR_S3WU_IRQHandler();
void __attribute__((weak)) SDMMC1_IRQHandler();
void __attribute__((weak)) SDMMC2_IRQHandler();
void __attribute__((weak)) GPDMA1_Channel8_IRQHandler();
void __attribute__((weak)) GPDMA1_Channel9_IRQHandler();
void __attribute__((weak)) GPDMA1_Channel10_IRQHandler();
void __attribute__((weak)) GPDMA1_Channel11_IRQHandler();
void __attribute__((weak)) GPDMA1_Channel12_IRQHandler();
void __attribute__((weak)) GPDMA1_Channel13_IRQHandler();
void __attribute__((weak)) GPDMA1_Channel14_IRQHandler();
void __attribute__((weak)) GPDMA1_Channel15_IRQHandler();
void __attribute__((weak)) I2C3_EV_IRQHandler();
void __attribute__((weak)) I2C3_ER_IRQHandler();
void __attribute__((weak)) SAI1_IRQHandler();
void __attribute__((weak)) SAI2_IRQHandler();
void __attribute__((weak)) TSC_IRQHandler();
void __attribute__((weak)) RNG_IRQHandler();
void __attribute__((weak)) FPU_IRQHandler();
void __attribute__((weak)) HASH_IRQHandler();
void __attribute__((weak)) LPTIM3_IRQHandler();
void __attribute__((weak)) SPI3_IRQHandler();
void __attribute__((weak)) I2C4_ER_IRQHandler();
void __attribute__((weak)) I2C4_EV_IRQHandler();
void __attribute__((weak)) MDF1_FLT0_IRQHandler();
void __attribute__((weak)) MDF1_FLT1_IRQHandler();
void __attribute__((weak)) MDF1_FLT2_IRQHandler();
void __attribute__((weak)) MDF1_FLT3_IRQHandler();
void __attribute__((weak)) UCPD1_IRQHandler();
void __attribute__((weak)) ICACHE_IRQHandler();
void __attribute__((weak)) LPTIM4_IRQHandler();
void __attribute__((weak)) DCACHE1_IRQHandler();
void __attribute__((weak)) ADF1_IRQHandler();
void __attribute__((weak)) ADC4_IRQHandler();
void __attribute__((weak)) LPDMA1_Channel0_IRQHandler();
void __attribute__((weak)) LPDMA1_Channel1_IRQHandler();
void __attribute__((weak)) LPDMA1_Channel2_IRQHandler();
void __attribute__((weak)) LPDMA1_Channel3_IRQHandler();
void __attribute__((weak)) DMA2D_IRQHandler();
void __attribute__((weak)) DCMI_PSSI_IRQHandler();
void __attribute__((weak)) OCTOSPI2_IRQHandler();
void __attribute__((weak)) MDF1_FLT4_IRQHandler();
void __attribute__((weak)) MDF1_FLT5_IRQHandler();
void __attribute__((weak)) CORDIC_IRQHandler();
void __attribute__((weak)) FMAC_IRQHandler();
void __attribute__((weak)) LSECSSD_IRQHandler();

// Stack top, needed to fill first entry of interrupt vector
extern char _main_stack_top asm("_main_stack_top");

// Interrupt vectors, must be placed @ address 0x00000000
// The extern declaration is required otherwise g++ optimizes it out
extern void (*const __Vectors[])();
void (*const __Vectors[])() __attribute__((section(".isr_vector"))) = {
    reinterpret_cast<void (*)()>(&_main_stack_top), // Stack pointer
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0, // SecureFault, unused: no TrustZone
    0,
    0,
    0,
    SVC_Handler,
    DebugMon_Handler,
    0,
    PendSV_Handler,
    SysTick_Handler,

    WWDG_IRQHandler,
    PVD_PVM_IRQHandler,
    RTC_IRQHandler,
    RTC_S_IRQHandler,
    TAMP_IRQHandler,
    RAMCFG_IRQHandler,
    FLASH_IRQHandler,
    FLASH_S_IRQHandler,
    GTZC_IRQHandler,
    RCC_IRQHandler,
    RCC_S_IRQHandler,
    EXTI0_IRQHandler,
    EXTI1_IRQHandler,
    EXTI2_IRQHandler,
    EXTI3_IRQHandler,
    EXTI4_IRQHandler,
    EXTI5_IRQHandler,
    EXTI6_IRQHandler,
    EXTI7_IRQHandler,
    EXTI8_IRQHandler,
    EXTI9_IRQHandler,
    EXTI10_IRQHandler,
    EXTI11_IRQHandler,
    EXTI12_IRQHandler,
    EXTI13_IRQHandler,
    EXTI14_IRQHandler,
    EXTI15_IRQHandler,
    IWDG_IRQHandler,
    0,
    GPDMA1_Channel0_IRQHandler,
    GPDMA1_Channel1_IRQHandler,
    GPDMA1_Channel2_IRQHandler,
    GPDMA1_Channel3_IRQHandler,
    GPDMA1_Channel4_IRQHandler,
    GPDMA1_Channel5_IRQHandler,
    GPDMA1_Channel6_IRQHandler,
    GPDMA1_Channel7_IRQHandler,
    ADC1_IRQHandler,
    DAC1_IRQHandler,
    FDCAN1_IT0_IRQHandler,
    FDCAN1_IT1_IRQHandler,
    TIM1_BRK_IRQHandler,
    TIM1_UP_IRQHandler,
    TIM1_TRG_COM_IRQHandler,
    TIM1_CC_IRQHandler,
    TIM2_IRQHandler,
    TIM3_IRQHandler,
    TIM4_IRQHandler,
    TIM5_IRQHandler,
    TIM6_IRQHandler,
    TIM7_IRQHandler,
    TIM8_BRK_IRQHandler,
    TIM8_UP_IRQHandler,
    TIM8_TRG_COM_IRQHandler,
    TIM8_CC_IRQHandler,
    I2C1_EV_IRQHandler,
    I2C1_ER_IRQHandler,
    I2C2_EV_IRQHandler,
    I2C2_ER_IRQHandler,
    SPI1_IRQHandler,
    SPI2_IRQHandler,
    USART1_IRQHandler,
    USART2_IRQHandler,
    USART3_IRQHandler,
    UART4_IRQHandler,
    UART5_IRQHandler,
    LPUART1_IRQHandler,
    LPTIM1_IRQHandler,
    LPTIM2_IRQHandler,
    TIM15_IRQHandler,
    TIM16_IRQHandler,
    TIM17_IRQHandler,
    COMP_IRQHandler,
    OTG_FS_IRQHandler,
    CRS_IRQHandler,
    FMC_IRQHandler,
    OCTOSPI1_IRQHandler,
    PWR_S3WU_IRQHandler,
    SDMMC1_IRQHandler,
    SDMMC2_IRQHandler,
    GPDMA1_Channel8_IRQHandler,
    GPDMA1_Channel9_IRQHandler,
    GPDMA1_Channel10_IRQHandler,
    GPDMA1_Channel11_IRQHandler,
    GPDMA1_Channel12_IRQHandler,
    GPDMA1_Channel13_IRQHandler,
    GPDMA1_Channel14_IRQHandler,
    GPDMA1_Channel15_IRQHandler,
    I2C3_EV_IRQHandler,
    I2C3_ER_IRQHandler,
    SAI1_IRQHandler,
    SAI2_IRQHandler,
    TSC_IRQHandler,
    0,
    RNG_IRQHandler,
    FPU_IRQHandler,
    HASH_IRQHandler,
    0,
    LPTIM3_IRQHandler,
    SPI3_IRQHandler,
    I2C4_ER_IRQHandler,
    I2C4_EV_IRQHandler,
    MDF1_FLT0_IRQHandler,
    MDF1_FLT1_IRQHandler,
    MDF1_FLT2_IRQHandler,
    MDF1_FLT3_IRQHandler,
    UCPD1_IRQHandler,
    ICACHE_IRQHandler,
    0,
    0,
    LPTIM4_IRQHandler,
    DCACHE1_IRQHandler,
    ADF1_IRQHandler,
    ADC4_IRQHandler,
    LPDMA1_Channel0_IRQHandler,
    LPDMA1_Channel1_IRQHandler,
    LPDMA1_Channel2_IRQHandler,
    LPDMA1_Channel3_IRQHandler,
    DMA2D_IRQHandler,
    DCMI_PSSI_IRQHandler,
    OCTOSPI2_IRQHandler,
    MDF1_FLT4_IRQHandler,
    MDF1_FLT5_IRQHandler,
    CORDIC_IRQHandler,
    FMAC_IRQHandler,
    LSECSSD_IRQHandler,
};

#pragma weak WWDG_IRQHandler = Default_Handler
#pragma weak PVD_PVM_IRQHandler = Default_Handler
#pragma weak RTC_IRQHandler = Default_Handler
#pragma weak RTC_S_IRQHandler = Default_Handler
#pragma weak TAMP_IRQHandler = Default_Handler
#pragma weak RAMCFG_IRQHandler = Default_Handler
#pragma weak FLASH_IRQHandler = Default_Handler
#pragma weak FLASH_S_IRQHandler = Default_Handler
#pragma weak GTZC_IRQHandler = Default_Handler
#pragma weak RCC_IRQHandler = Default_Handler
#pragma weak RCC_S_IRQHandler = Default_Handler
#pragma weak EXTI0_IRQHandler = Default_Handler
#pragma weak EXTI1_IRQHandler = Default_Handler
#pragma weak EXTI2_IRQHandler = Default_Handler
#pragma weak EXTI3_IRQHandler = Default_Handler
#pragma weak EXTI4_IRQHandler = Default_Handler
#pragma weak EXTI5_IRQHandler = Default_Handler
#pragma weak EXTI6_IRQHandler = Default_Handler
#pragma weak EXTI7_IRQHandler = Default_Handler
#pragma weak EXTI8_IRQHandler = Default_Handler
#pragma weak EXTI9_IRQHandler = Default_Handler
#pragma weak EXTI10_IRQHandler = Default_Handler
#pragma weak EXTI11_IRQHandler = Default_Handler
#pragma weak EXTI12_IRQHandler = Default_Handler
#pragma weak EXTI13_IRQHandler = Default_Handler
#pragma weak EXTI14_IRQHandler = Default_Handler
#pragma weak EXTI15_IRQHandler = Default_Handler
#pragma weak IWDG_IRQHandler = Default_Handler
#pragma weak GPDMA1_Channel0_IRQHandler = Default_Handler
#pragma weak GPDMA1_Channel1_IRQHandler = Default_Handler
#pragma weak GPDMA1_Channel2_IRQHandler = Default_Handler
#pragma weak GPDMA1_Channel3_IRQHandler = Default_Handler
#pragma weak GPDMA1_Channel4_IRQHandler = Default_Handler
#pragma weak GPDMA1_Channel5_IRQHandler = Default_Handler
#pragma weak GPDMA1_Channel6_IRQHandler = Default_Handler
#pragma weak GPDMA1_Channel7_IRQHandler = Default_Handler
#pragma weak ADC1_IRQHandler = Default_Handler
#pragma weak DAC1_IRQHandler = Default_Handler
#pragma weak FDCAN1_IT0_IRQHandler = Default_Handler
#pragma weak FDCAN1_IT1_IRQHandler = Default_Handler
#pragma weak TIM1_BRK_IRQHandler = Default_Handler
#pragma weak TIM1_UP_IRQHandler = Default_Handler
#pragma weak TIM1_TRG_COM_IRQHandler = Default_Handler
#pragma weak TIM1_CC_IRQHandler = Default_Handler
#pragma weak TIM2_IRQHandler = Default_Handler
#pragma weak TIM3_IRQHandler = Default_Handler
#pragma weak TIM4_IRQHandler = Default_Handler
#pragma weak TIM5_IRQHandler = Default_Handler
#pragma weak TIM6_IRQHandler = Default_Handler
#pragma weak TIM7_IRQHandler = Default_Handler
#pragma weak TIM8_BRK_IRQHandler = Default_Handler
#pragma weak TIM8_UP_IRQHandler = Default_Handler
#pragma weak TIM8_TRG_COM_IRQHandler = Default_Handler
#pragma weak TIM8_CC_IRQHandler = Default_Handler
#pragma weak I2C1_EV_IRQHandler = Default_Handler
#pragma weak I2C1_ER_IRQHandler = Default_Handler
#pragma weak I2C2_EV_IRQHandler = Default_Handler
#pragma weak I2C2_ER_IRQHandler = Default_Handler
#pragma weak SPI1_IRQHandler = Default_Handler
#pragma weak SPI2_IRQHandler = Default_Handler
#pragma weak USART1_IRQHandler = Default_Handler
#pragma weak USART2_IRQHandler = Default_Handler
#pragma weak USART3_IRQHandler = Default_Handler
#pragma weak UART4_IRQHandler = Default_Handler
#pragma weak UART5_IRQHandler = Default_Handler
#pragma weak LPUART1_IRQHandler = Default_Handler
#pragma weak LPTIM1_IRQHandler = Default_Handler
#pragma weak LPTIM2_IRQHandler = Default_Handler
#pragma weak TIM15_IRQHandler = Default_Handler
#pragma weak TIM16_IRQHandler = Default_Handler
#pragma weak TIM17_IRQHandler = Default_Handler
#pragma weak COMP_IRQHandler = Default_Handler
#pragma weak OTG_FS_IRQHandler = Default_Handler
#pragma weak CRS_IRQHandler = Default_Handler
#pragma weak FMC_IRQHandler = Default_Handler
#pragma weak OCTOSPI1_IRQHandler = Default_Handler
#pragma weak PWR_S3WU_IRQHandler = Default_Handler
#pragma weak SDMMC1_IRQHandler = Default_Handler
#pragma weak SDMMC2_IRQHandler = Default_Handler
#pragma weak GPDMA1_Channel8_IRQHandler = Default_Handler
#pragma weak GPDMA1_Channel9_IRQHandler = Default_Handler
#pragma weak GPDMA1_Channel10_IRQHandler = Default_Handler
#pragma weak GPDMA1_Channel11_IRQHandler = Default_Handler
#pragma weak GPDMA1_Channel12_IRQHandler = Default_Handler
#pragma weak GPDMA1_Channel13_IRQHandler = Default_Handler
#pragma weak GPDMA1_Channel14_IRQHandler = Default_Handler
#pragma weak GPDMA1_Channel15_IRQHandler = Default_Handler
#pragma weak I2C3_EV_IRQHandler = Default_Handler
#pragma weak I2C3_ER_IRQHandler = Default_Handler
#pragma weak SAI1_IRQHandler = Default_Handler
#pragma weak SAI2_IRQHandler = Default_Handler
#pragma weak TSC_IRQHandler = Default_Handler
#pragma weak RNG_IRQHandler = Default_Handler
#pragma weak FPU_IRQHandler = Default_Handler
#pragma weak HASH_IRQHandler = Default_Handler
#pragma weak LPTIM3_IRQHandler = Default_Handler
#pragma weak SPI3_IRQHandler = Default_Handler
#pragma weak I2C4_ER_IRQHandler = Default_Handler
#pragma weak I2C4_EV_IRQHandler = Default_Handler
#pragma weak MDF1_FLT0_IRQHandler = Default_Handler
#pragma weak MDF1_FLT1_IRQHandler = Default_Handler
#pragma weak MDF1_FLT2_IRQHandler = Default_Handler
#pragma weak MDF1_FLT3_IRQHandler = Default_Handler
#pragma weak UCPD1_IRQHandler = Default_Handler
#pragma weak ICACHE_IRQHandler = Default_Handler
#pragma weak LPTIM4_IRQHandler = Default_Handler
#pragma weak DCACHE1_IRQHandler = Default_Handler
#pragma weak ADF1_IRQHandler = Default_Handler
#pragma weak ADC4_IRQHandler = Default_Handler
#pragma weak LPDMA1_Channel0_IRQHandler = Default_Handler
#pragma weak LPDMA1_Channel1_IRQHandler = Default_Handler
#pragma weak LPDMA1_Channel2_IRQHandler = Default_Handler
#pragma weak LPDMA1_Channel3_IRQHandler = Default_Handler
#pragma weak DMA2D_IRQHandler = Default_Handler
#pragma weak DCMI_PSSI_IRQHandler = Default_Handler
#pragma weak OCTOSPI2_IRQHandler = Default_Handler
#pragma weak MDF1_FLT4_IRQHandler = Default_Handler
#pragma weak MDF1_FLT5_IRQHandler = Default_Handler
#pragma weak CORDIC_IRQHandler = Default_Handler
#pragma weak FMAC_IRQHandler = Default_Handler
#pragma weak LSECSSD_IRQHandler = Default_Handler
