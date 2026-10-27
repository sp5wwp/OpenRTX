/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef BASEBAND_DUMP_C62_H
#define BASEBAND_DUMP_C62_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Raw RX baseband dump over the console UART (Kenwood connector, TX line).
 *
 * The 48 kHz samples the CSK6 ADC takes from the BK4819 AF output are framed
 * into packets and sent over UART2 at C62_BASEBAND_DUMP_BAUD. While the dump
 * is active, logging and printk are muted so they cannot corrupt the stream.
 * The DSP firmware and the boot log can still put stray bytes on the line:
 * the receiver resynchronises on the packet magic and checks a CRC.
 *
 * Packet layout, little endian:
 *   0  u8[4] magic A5 5A C3 3C
 *   4  u8    version (1)
 *   5  u8    format: 1 = s16, 2 = 12-bit packed (2 samples in 3 bytes)
 *   6  u16   packet sequence number
 *   8  u32   index of the first sample, counted at 48 kHz from dump start
 *   12 u16   number of samples
 *   14 u16   flags: bit 0 = capture (re)started before this packet
 *   16 u16   BK4819 REG_43 value in use for RX
 *   18 i16   RSSI in dBm, as filtered by the rtx thread
 *   20 u16   payload length in bytes
 *   22       payload
 *   ...      u16 CRC-16/CCITT-FALSE over bytes 4 to the end of the payload
 *
 * Dropped packets show as gaps in the sequence number and sample index.
 */

#define BBDUMP_SAMPLE_RATE 48000

/**
 * Switch the console UART to the dump baud rate, mute logging and start the
 * sender thread. Call after the audio codec is initialized, as the DSP
 * reconfigures the console UART while it starts. Safe to call again.
 */
void baseband_dump_init(void);

/**
 * Mark a capture discontinuity: the next packet carries the restart flag.
 * Called by the codec worker when capture starts.
 */
void baseband_dump_restart(void);

/**
 * Add received samples to the dump. Called by the codec worker only.
 *
 * @param samples: 48 kHz radio samples.
 * @param count: number of samples.
 */
void baseband_dump_push(const int16_t *samples, size_t count);

/**
 * Check whether samples are being recorded, rather than dropped while a
 * burst is being sent.
 */
bool baseband_dump_recording(void);

#ifdef __cplusplus
}
#endif

#endif /* BASEBAND_DUMP_C62_H */
