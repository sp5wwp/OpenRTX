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

/*
 * Codec2-mod, code and tables: the encoder did not keep up with real time
 * from the slow area, and the M17 transmission underran.
 */
*libapp.a:analysis.c.obj(.text .text.* .rodata .rodata.*)
*libapp.a:codec2_mod.c.obj(.text .text.* .rodata .rodata.*)
*libapp.a:delta_lsp_cb.c.obj(.text .text.* .rodata .rodata.*)
*libapp.a:interp.c.obj(.text .text.* .rodata .rodata.*)
*libapp.a:kiss_fft.c.obj(.text .text.* .rodata .rodata.*)
*libapp.a:kiss_fftr.c.obj(.text .text.* .rodata .rodata.*)
*libapp.a:lpc.c.obj(.text .text.* .rodata .rodata.*)
*libapp.a:nlp.c.obj(.text .text.* .rodata .rodata.*)
*libapp.a:quantise.c.obj(.text .text.* .rodata .rodata.*)
*libapp.a:synthesis.c.obj(.text .text.* .rodata .rodata.*)
*libapp.a:util.c.obj(.text .text.* .rodata .rodata.*)

/*
 * M17 modulator, demodulator and their filter taps: the 48 kHz RRC filter of
 * the modulator read its taps from the slow area for every product, and took
 * longer than the 40 ms of a frame.
 */
*libapp.a:DSP.cpp.obj(.text .text.* .rodata .rodata.*)
*libapp.a:Modulator.cpp.obj(.text .text.* .rodata .rodata.*)
*libapp.a:Demodulator.cpp.obj(.text .text.* .rodata .rodata.*)
