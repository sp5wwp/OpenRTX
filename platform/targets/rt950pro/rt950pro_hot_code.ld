/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Only the first 128 kB of Flash are zero-wait when the EOPB0 option byte
 * selects 224 kB of SRAM (256 kB with the default 96 kB): code above runs
 * many times slower. The kernel, its interrupt entry and exit, the timer,
 * serial, GPIO and SPI drivers, the audio DMA interrupt code and the runtime
 * libraries are placed here, right after the model signatures: their
 * interrupt-masked sections otherwise held off the serial receive interrupt
 * long enough for it to lose characters.
 */
*libkernel.a:*(.text .text.*)
*libarch__arm__core__cortex_m.a:*(.text .text.*)
*libarch__arm__core.a:*(.text .text.*)
*libarch__common.a:*(.text .text.*)
*libdrivers__timer.a:*(.text .text.*)
*libdrivers__serial.a:*(.text .text.*)
*libdrivers__gpio.a:*(.text .text.*)
*libdrivers__spi.a:*(.text .text.*)
*libapp.a:at32_adc.c.obj(.text .text.* .rodata .rodata.*)
*libapp.a:at32_dac.c.obj(.text .text.* .rodata .rodata.*)
*libapp.a:BK4829_tone.cpp.obj(.text .text.* .rodata .rodata.*)
*libapp.a:audio_rt950pro.c.obj(.text .text.* .rodata .rodata.*)
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
