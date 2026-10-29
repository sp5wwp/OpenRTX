/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * This file uses the AT32 HAL register definitions: it must not include any
 * Zephyr GPIO driver header, whose flag macros clash with the HAL enums.
 */
#include <soc.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/linker-defs.h>
#include <zephyr/platform/hooks.h>

/*
 * Model signatures checked by the vendor bootloader, placed at image offsets
 * 0x3E0 and 0x7E0 by rt950pro_model_sig.ld.
 */
__attribute__((section(".rt950_model_sig"), used))
const uint8_t rt950_model_sig[16] = { 'R', 'T', '-', '9', '5', '0', ' ', ' ',
                                      ' ', ' ', ' ', ' ', 0,   0,   0,   0 };

__attribute__((section(".rt950_boot_model_sig"), used))
const uint8_t rt950_boot_model_sig[16] = { 'R', 'T', '-', '9', '5', '0',
                                           ' ', ' ', ' ', ' ', ' ', ' ',
                                           0,   0,   0,   0 };

void __inline__ rt950_iwdg_feed(void)
{
    WDT->cmd = WDT_CMD_RELOAD;
}

void board_early_init_hook(void)
{
    /*
     * The SoC early init hook calls the HAL SystemInit(), which resets the
     * vector table address to the beginning of the flash, where the vendor
     * bootloader lives. Point it back to the OpenRTX vector table.
     */
    SCB->VTOR = (uint32_t)_vector_start & SCB_VTOR_TBLOFF_Msk;
    __DSB();
    __ISB();

    rt950_iwdg_feed();

    /*
     * Disable JTAG keeping SWD: PB3 (keypad backlight) and PB4 (encoder A)
     * are JTAG pins after reset.
     */
    CRM->apb2en |= (1UL << 0);
    (void)CRM->apb2en;
    IOMUX->remap7 = (IOMUX->remap7 & ~(0x7UL << 16)) | (0x2UL << 16);

    /*
     * Keep the LCD (PC6) and keypad (PB3) backlights off until something has
     * been drawn on
     * the panel, which otherwise shows the content left by the bootloader.
     */
    CRM->apb2en |= (1UL << 3) | (1UL << 4);
    (void)CRM->apb2en;
    GPIOC->clr = (1UL << 6);
    GPIOC->cfglr = (GPIOC->cfglr & ~(0xFUL << 24)) | (0x2UL << 24);
    GPIOB->clr = (1UL << 3);
    GPIOB->cfglr = (GPIOB->cfglr & ~(0xFUL << 12)) | (0x2UL << 12);
}

/*
 * The independent watchdog started by the vendor bootloader cannot be
 * stopped: feed it from the lowest priority thread, so that a system starved
 * by a stuck higher priority thread still gets reset. The thread is started
 * as soon as the kernel is up, so that it also runs while the device drivers
 * initialization sleeps (e.g. the display controller power-up sequence).
 */
#define IWDG_FEEDER_STACK_SIZE 384

static K_THREAD_STACK_DEFINE(iwdgFeederStack, IWDG_FEEDER_STACK_SIZE);
static struct k_thread iwdgFeederThread;

static void iwdgFeeder(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (true) {
        rt950_iwdg_feed();
        k_msleep(250);
    }
}

static int iwdgFeederStart(void)
{
    k_thread_create(&iwdgFeederThread, iwdgFeederStack,
                    K_THREAD_STACK_SIZEOF(iwdgFeederStack), iwdgFeeder, NULL,
                    NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);
    k_thread_name_set(&iwdgFeederThread, "iwdg");

    return 0;
}

SYS_INIT(iwdgFeederStart, POST_KERNEL, 0);
