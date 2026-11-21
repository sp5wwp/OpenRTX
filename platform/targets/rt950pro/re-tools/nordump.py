#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Dump the RT-950 Pro external SPI NOR flash (factory data, calibration)
# through the OpenRTX monitor, read only ("sf r" / "sf c").
#
#   nordump.py [--port /dev/ttyUSB0] <output.bin> [start] [length]
#
# Every 4 kB chunk is checked: its lines must carry the requested addresses,
# and its CRC-32 must match the one the radio computes ("sf c"). A chunk that
# fails is read again: a garbled command or a late reply then costs a retry,
# not a corrupted dump.

import sys
import time
import zlib

from monitor import Monitor

CHUNK = 0x1000
SIZE = 4 * 1024 * 1024


def drain(m):
    time.sleep(0.2)
    m.p.reset_input_buffer()


def read_chunk(m, addr):
    for attempt in range(20):
        try:
            lines = m.cmd(f'sf r {addr:x} {CHUNK:x}')
            data = bytearray()
            for l in lines:
                a, h = l.split()
                if int(a, 16) != addr + len(data):
                    raise ValueError('address out of sequence')
                data += bytes.fromhex(h)
            if len(data) != CHUNK:
                raise ValueError('short chunk')

            crc = m.cmd(f'sf c {addr:x} {CHUNK:x}')
            if (len(crc) != 1) or (int(crc[0], 16) != zlib.crc32(data)):
                raise ValueError('CRC mismatch')

            return bytes(data)
        except (RuntimeError, TimeoutError, ValueError):
            drain(m)

    raise RuntimeError(f'chunk {addr:06X}: no good read')


def main():
    args = sys.argv[1:]
    port = '/dev/ttyUSB0'
    if args[:1] == ['--port']:
        port, args = args[1], args[2:]
    out = args[0]
    start = int(args[1], 16) if len(args) > 1 else 0
    length = int(args[2], 16) if len(args) > 2 else SIZE

    m = Monitor(port)
    print('JEDEC ID', m.cmd('sf id')[0])

    t0 = time.time()
    with open(out, 'wb') as f:
        for addr in range(start, start + length, CHUNK):
            f.write(read_chunk(m, addr))
            done = addr + CHUNK - start
            if (done % 0x40000) == 0:
                rate = done / (time.time() - t0)
                print(f'{done >> 10} kB, {(length - done) / rate:.0f} s left',
                      flush=True)

    m.p.timeout = 30  # the CRC of the whole flash takes several seconds
    crc = m.cmd(f'sf c {start:x} {length:x}')[0]
    with open(out, 'rb') as f:
        ok = int(crc, 16) == zlib.crc32(f.read())
    print(f'whole range CRC {crc}: {"match" if ok else "MISMATCH"}')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
