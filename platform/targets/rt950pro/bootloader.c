/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <zephyr/fatal.h>
#include <zephyr/irq.h>
#include <zephyr/sys/reboot.h>
#include "bootloader.h"

/*
 * Restart into the stock bootloader's update mode without pressing keys.
 *
 * The bootloader (0x08000000-0x08002FFF) enters update mode when the two side
 * keys (PA12, PE5) read low at power-on. It also does so, at every start, when
 * the frame AA 42 00 00 00 00 55 EB 55 arrives on UART4 (programming cable,
 * 115200 8N1) within a window of 200 SysTick periods after its UART set-up.
 * The frame is acknowledged with AA 42 00 06 00 00 E7 4B 55, see the loop at
 * 0x0800056C of the bootloader.
 *
 * The stock firmware uses this window: after the "PROGRAMBT9000U" and
 * "UPDATE" handshake it only performs a system reset (V0.30, 0x08011A2E and
 * 0x0801CDAC), and the update tool floods the frame. The same is done here:
 * no flag in RAM and no jump into the bootloader are needed.
 */

void bootloader_restartToUpdate()
{
    (void)irq_lock();
    sys_reboot(SYS_REBOOT_COLD);

    while (1)
        ;
}

/*
 * Restart on a fatal error instead of halting, so that the bootloader start-up
 * window keeps keyless updates possible after a crash. Overrides the weak
 * handler of the Zephyr kernel (kernel/fatal.c), after its error report.
 */
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
    (void)reason;
    (void)esf;

    sys_reboot(SYS_REBOOT_COLD);
}
