/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * With 224 kB of SRAM (EOPB0, optionbytes.c) only the first 128 kB of Flash
 * are zero-wait: code above runs many times slower. The kernel, its interrupt
 * entry and exit, the timer and serial drivers and the runtime libraries are
 * placed here, after the audio interrupt code: their interrupt-masked sections
 * otherwise held off the audio interrupt for up to 180 us, and the serial
 * receive interrupt overran.
 */
*libkernel.a:*(.text .text.*)
*libarch__arm__core__cortex_m.a:*(.text .text.*)
*libarch__arm__core.a:*(.text .text.*)
*libarch__common.a:*(.text .text.*)
*libdrivers__timer.a:*(.text .text.*)
*libdrivers__serial.a:*(.text .text.*)
*libdrivers__gpio.a:*(.text .text.*)
*libdrivers__interrupt_controller.a:*(.text .text.*)
*libzephyr.a:*(.text .text.*)
*liblib__posix__options.a:*(.text .text.*)
*libgcc.a:*(.text .text.*)
*libm.a:*(.text .text.*)
*libc.a:*mem*(.text .text.*)
*libc.a:*str*(.text .text.*)
