#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Receive the Retevis C62 raw RX baseband dump (C62_BASEBAND_DUMP builds).

The radio streams 48 kHz samples of the BK4819 AF output over the console
UART, framed in packets (see platform/drivers/audio/baseband_dump_c62.h).
This tool finds the packets in the byte stream, checks their CRC, puts the
samples back on a continuous timeline and writes:

  OUT_burstNNN.wav   48 kHz mono 16-bit WAV, for listening or sox
  OUT_burstNNN.f32   raw float32 in [-1, 1), for a GNU Radio File Source
  OUT_burstNNN.csv   one row per packet: sample index, REG_43, RSSI, flags

A new burst starts at every capture (re)start: each burst is gapless, and the
default burst-mode build records ~10.4 s per burst.

Missing packets are filled with zeros so the timeline stays exact (use
--no-fill to concatenate instead). With --split-reg43 each REG_43 value gets
its own files, for the C62_REG43_SWEEP builds.

Examples:
  # Default build (burst mode, 115200 baud): each burst is ~10.4 s of
  # gapless audio and takes ~91 s to arrive. Capture one burst:
  c62_bbdump.py -p /dev/ttyUSB0 -t 100 -o tetra
  # Keep the raw bytes too, then re-parse them later
  c62_bbdump.py -p /dev/ttyUSB0 --raw raw.bin -o tetra
  c62_bbdump.py -i raw.bin -o tetra --split-reg43

Live capture needs pyserial (pip install pyserial); parsing a file needs
nothing but the standard library.
"""

import argparse
import array
import struct
import sys
import time
import wave

MAGIC = b"\xa5\x5a\xc3\x3c"
HEADER = struct.Struct("<4sBBHIHHHhH")  # 22 bytes
SAMPLE_RATE = 48000
MAX_PAYLOAD = 4096
FLAG_RESTART = 1


def crc16(data):
    """CRC-16/CCITT-FALSE."""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


try:  # Much faster when available
    import binascii

    def crc16(data):  # noqa: F811
        return binascii.crc_hqx(bytes(data), 0xFFFF)
except ImportError:
    pass


def unpack12(payload, count):
    out = array.array("h")
    for i in range(0, len(payload) - 2, 3):
        b0, b1, b2 = payload[i], payload[i + 1], payload[i + 2]
        a = b0 | ((b1 & 0x0F) << 8)
        b = (b1 >> 4) | (b2 << 4)
        for v in (a, b):
            if v & 0x800:
                v -= 0x1000
            out.append(v << 4)
    return out[:count]


class Packet:
    __slots__ = ("seq", "index", "flags", "reg43", "rssi", "fmt", "samples")


class Parser:
    """Incremental parser: feed() bytes, collect Packet objects."""

    def __init__(self):
        self.buf = bytearray()
        self.packets_ok = 0
        self.crc_errors = 0
        self.skipped_bytes = 0

    def feed(self, data):
        self.buf += data
        out = []
        while True:
            start = self.buf.find(MAGIC)
            if start < 0:
                keep = len(MAGIC) - 1
                self.skipped_bytes += max(0, len(self.buf) - keep)
                del self.buf[:-keep or None]
                return out
            if start:
                self.skipped_bytes += start
                del self.buf[:start]
            if len(self.buf) < HEADER.size:
                return out
            (_, version, fmt, seq, index, count, flags, reg43, rssi,
             plen) = HEADER.unpack_from(self.buf)
            if version != 1 or fmt not in (1, 2) or plen > MAX_PAYLOAD:
                self.skipped_bytes += 1
                del self.buf[:1]
                continue
            total = HEADER.size + plen + 2
            if len(self.buf) < total:
                return out
            body = self.buf[4:HEADER.size + plen]
            (crc,) = struct.unpack_from("<H", self.buf, HEADER.size + plen)
            if crc16(body) != crc:
                # Not a packet, or a corrupted one: resync past this magic
                self.crc_errors += 1
                self.skipped_bytes += 1
                del self.buf[:1]
                continue
            payload = bytes(self.buf[HEADER.size:HEADER.size + plen])
            del self.buf[:total]
            p = Packet()
            p.seq, p.index, p.flags = seq, index, flags
            p.reg43, p.rssi, p.fmt = reg43, rssi, fmt
            if fmt == 1:
                p.samples = array.array("h", payload)
                if sys.byteorder != "little":
                    p.samples.byteswap()
            else:
                p.samples = unpack12(payload, count)
            self.packets_ok += 1
            out.append(p)


class Sink:
    """Writes one output set (wav, f32, csv) on a continuous timeline."""

    def __init__(self, prefix, fill):
        self.prefix = prefix
        self.fill = fill
        self.wav = wave.open(prefix + ".wav", "wb")
        self.wav.setnchannels(1)
        self.wav.setsampwidth(2)
        self.wav.setframerate(SAMPLE_RATE)
        self.f32 = open(prefix + ".f32", "wb")
        self.csv = open(prefix + ".csv", "w")
        self.csv.write("seq,sample_index,out_sample,flags,reg43,rssi_dbm\n")
        self.next_index = None
        self.written = 0
        self.gap_samples = 0
        self.gaps = 0
        self.peak = 0
        self.clipped = 0
        self.sumsq = 0.0
        self.rssi = []

    def _write(self, samples):
        le = array.array("h", samples)
        if sys.byteorder != "little":
            le.byteswap()
        self.wav.writeframesraw(le.tobytes())
        f = array.array("f", (s / 32768.0 for s in samples))
        self.f32.write(f.tobytes())
        self.written += len(samples)

    def add(self, p):
        if self.next_index is not None and p.index != self.next_index:
            missing = (p.index - self.next_index) & 0xFFFFFFFF
            if 0 < missing < SAMPLE_RATE * 60:
                self.gaps += 1
                self.gap_samples += missing
                if self.fill:
                    self._write(array.array("h", bytes(2 * missing)))
        self.csv.write("%d,%d,%d,%d,0x%04x,%d\n" % (
            p.seq, p.index, self.written, p.flags, p.reg43, p.rssi))
        full = 32767 if p.fmt == 1 else 2047 << 4
        for s in p.samples:
            a = abs(s)
            if a > self.peak:
                self.peak = a
            if a >= full - 16:
                self.clipped += 1
            self.sumsq += float(s) * s
        self.rssi.append(p.rssi)
        self._write(p.samples)
        self.next_index = (p.index + len(p.samples)) & 0xFFFFFFFF

    def close(self):
        self.wav.close()
        self.f32.close()
        self.csv.close()

    def report(self, name):
        n = max(1, self.written - (self.gap_samples if self.fill else 0))
        rms = (self.sumsq / n) ** 0.5
        rssi = sum(self.rssi) / len(self.rssi) if self.rssi else float("nan")
        print("%-14s %8.2f s  gaps %d (%.3f s)  peak %5d (%5.1f dBFS)  "
              "rms %5.1f dBFS  clipped %d  rssi %.0f dBm" % (
                  name, self.written / SAMPLE_RATE, self.gaps,
                  self.gap_samples / SAMPLE_RATE, self.peak,
                  _db(self.peak / 32768.0), _db(rms / 32768.0),
                  self.clipped, rssi))


def _db(x):
    import math
    return 20 * math.log10(x) if x > 0 else float("-inf")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("-p", "--port", help="serial port, e.g. /dev/ttyUSB0")
    src.add_argument("-i", "--input", help="parse a raw byte capture file")
    ap.add_argument("-b", "--baud", type=int, default=115200,
                    help="baud rate (default 115200, match the build)")
    ap.add_argument("-t", "--time", type=float, default=0,
                    help="stop after this many seconds (live capture)")
    ap.add_argument("-o", "--output", default="c62_baseband",
                    help="output file prefix (default c62_baseband)")
    ap.add_argument("--raw", help="also save the raw received bytes here")
    ap.add_argument("--split-reg43", action="store_true",
                    help="separate outputs per REG_43 value (sweep builds)")
    ap.add_argument("--join-bursts", action="store_true",
                    help="one output for all bursts instead of one per burst")
    ap.add_argument("--no-fill", action="store_true",
                    help="do not zero-fill missing packets")
    args = ap.parse_args()
    args.split_bursts = not args.join_bursts

    parser = Parser()
    sinks = {}
    raw = open(args.raw, "wb") if args.raw else None
    last_seq = None
    seq_gaps = 0
    restarts = 0
    burst = 0

    def handle(packets):
        nonlocal last_seq, seq_gaps, restarts, burst
        for p in packets:
            if last_seq is not None and p.seq != (last_seq + 1) & 0xFFFF:
                seq_gaps += 1
            last_seq = p.seq
            if p.flags & FLAG_RESTART:
                restarts += 1
            if args.split_bursts and p.flags & FLAG_RESTART:
                burst += 1
            key = p.reg43 if args.split_reg43 else None
            if args.split_bursts:
                key = (burst, key)
            if key not in sinks:
                reg43 = key[1] if args.split_bursts else key
                prefix = args.output
                if args.split_bursts:
                    prefix += "_burst%03d" % key[0]
                if reg43 is not None:
                    prefix += "_reg43_%04x" % reg43
                sinks[key] = Sink(prefix, not args.no_fill)
            sinks[key].add(p)

    try:
        if args.input:
            with open(args.input, "rb") as f:
                while True:
                    chunk = f.read(1 << 16)
                    if not chunk:
                        break
                    handle(parser.feed(chunk))
        else:
            import serial
            port = serial.Serial(args.port, args.baud, timeout=0.1)
            port.reset_input_buffer()
            start = last = time.monotonic()
            print("Capturing from %s at %d baud, Ctrl-C to stop" %
                  (args.port, args.baud), file=sys.stderr)
            while not args.time or time.monotonic() - start < args.time:
                chunk = port.read(max(1, port.in_waiting))
                if raw and chunk:
                    raw.write(chunk)
                handle(parser.feed(chunk))
                now = time.monotonic()
                if now - last >= 1.0:
                    last = now
                    total = sum(s.written for s in sinks.values())
                    print("\r%6.1f s  packets %d  crc errors %d  seq gaps %d"
                          "  audio %.1f s   " % (
                              now - start, parser.packets_ok,
                              parser.crc_errors, seq_gaps,
                              total / SAMPLE_RATE),
                          end="", file=sys.stderr)
            print(file=sys.stderr)
    except KeyboardInterrupt:
        print(file=sys.stderr)
    finally:
        if raw:
            raw.close()
        for s in sinks.values():
            s.close()

    print("packets %d, CRC errors %d, sequence gaps %d, capture restarts %d, "
          "non-packet bytes %d" % (parser.packets_ok, parser.crc_errors,
                                   seq_gaps, restarts, parser.skipped_bytes))
    for key, s in sorted(sinks.items(), key=lambda kv: str(kv[0])):
        s.report(s.prefix[len(args.output) + 1:] or "all")
    if not sinks:
        print("No packets found: check the baud rate and that the radio is "
              "in RX with the dump build.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
