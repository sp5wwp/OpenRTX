/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <hwconfig.h>
#include <stdbool.h>
#include <stdint.h>
#include "bootloader.h"

/*
 * Restart into the stock bootloader's update mode without pressing keys.
 *
 * The bootloader (0x08000000-0x0800BFFF) enters update mode only when PA8
 * (top key) and PA10 (PTT) read low, at 0x08006B2C. Otherwise it jumps to
 * the application with everything still set up: it only loads the stack
 * pointer and branches (0x08006A7C). So a request is a flag in RAM followed
 * by a system reset. The bootloader then starts OpenRTX normally, and the
 * first thing OpenRTX does is to look at the flag and, if set, branch to the
 * update branch of the bootloader, 0x08006B32, as if the keys were pressed.
 *
 * The flag lives at the top of SRAM3, which the bootloader does not use and
 * which the option bytes (SRAM_RST = 1) keep across a system reset.
 *
 * The addresses belong to the bootloader found on the development radio. It
 * is checked before use; another version is left alone.
 */

#define FLAG_ADDR 0x200BFFF8UL
#define FLAG_MAGIC 0x54445055UL      // "UPDT"
#define BL_STACK 0x20003818UL        // Bootloader stack, below its main frame
#define BL_UPDATE_ENTRY 0x08006B32UL // Update branch of the bootloader main

/* Code around the key check of the known bootloader, 0x08006B28 */
static const uint32_t blSignature[] = {
    0xFA18F7FD,
    0xFD88F7F9,
    0xF7FDB3B8,
    0x2000FB53,
};

bool bootloader_isKnown()
{
    const volatile uint32_t *code = (const volatile uint32_t *)0x08006B28UL;

    for (unsigned i = 0; i < sizeof(blSignature) / sizeof(blSignature[0]);
         i++) {
        if (code[i] != blSignature[i])
            return false;
    }

    return true;
}

void bootloader_restartToUpdate()
{
    volatile uint32_t *flag = (volatile uint32_t *)FLAG_ADDR;

    flag[0] = FLAG_MAGIC;
    flag[1] = ~FLAG_MAGIC;
    __DSB();

    NVIC_SystemReset();
}

/*
 * Called by Reset_Handler before anything else: .data and .bss are not set
 * up yet and the bootloader's clock and peripheral setup is still in place.
 */
void early_boot_hook()
{
    volatile uint32_t *flag = (volatile uint32_t *)FLAG_ADDR;

    if ((flag[0] != FLAG_MAGIC) || (flag[1] != ~FLAG_MAGIC))
        return;

    flag[0] = 0;
    flag[1] = 0;

    if (bootloader_isKnown() == false)
        return;

    asm volatile("msr msp, %0 \n\t"
                 "bx  %1      \n\t" ::"r"(BL_STACK),
                 "r"(BL_UPDATE_ENTRY | 1));
}
