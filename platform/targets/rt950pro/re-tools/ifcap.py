#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Capture the receiver's raw low-IF samples (PA2, 48kHz, ADC counts) through
# the debug monitor, optionally while a USRP B2xx sends an M17 burst.
#
#   ifcap.py <out.u16> [--ms 600] [--burst symbols.sym --gain 12]
#            [--delay 2.0] [--smute] [--port /dev/ttyUSB0]
#
# The radio has to be receiving M17 at the burst's frequency. --smute takes
# the decoded speech off the net the IF is read from, for the length of the
# capture. The file holds little-endian uint16 samples at 48kHz; the radio's
# M17 stream counters before and after are printed.

import argparse
import struct
import subprocess
import sys
import time

sys.dont_write_bytecode = True
from monitor import Monitor


def counters(m):
    line = [x for x in m.cmd('rx') if x.startswith('frames')][0].split()
    return int(line[1]), int(line[3]), int(line[5])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--ms', type=int, default=600)
    ap.add_argument('--burst')
    ap.add_argument('--gain', type=float, default=12)
    ap.add_argument('--delay', type=float, default=2.0)
    ap.add_argument('--smute', action='store_true')
    ap.add_argument('--port', default='/dev/ttyUSB0')
    a = ap.parse_args()

    m = Monitor(a.port)
    m.p.timeout = 5
    before = counters(m)
    if a.smute:
        m.cmd('smute on')

    tx = None
    if a.burst:
        tx = subprocess.Popen(['python3', '-B', 'm17_b210_tx.py', a.burst,
                               '--gain', str(a.gain)],
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL)
        time.sleep(a.delay)

    try:
        m.cmd(f'cap {a.ms}')
        while True:
            p = m.cmd('cap')[0].split()
            addr, count, size = int(p[1], 16), int(p[3]), int(p[5])
            if count >= size:
                break
            time.sleep(0.1)
        data = m.mem(addr, size * 2)
        m.cmd('cap free')
    finally:
        if a.smute:
            m.cmd('smute off')
        if tx is not None:
            tx.wait()

    after = counters(m)
    open(a.out, 'wb').write(data)
    samples = struct.unpack(f'<{size}H', data)
    print(f'{size} samples ({size / 48000:.2f}s) in {a.out}, '
          f'{min(samples)}..{max(samples)} counts; speech '
          f'{"muted" if a.smute else "playing"}')
    f, acc, err = (y - x for x, y in zip(before, after))
    print(f'stream frames during the run: {f}, accepted {acc}, '
          f'bit errors {err}')


if __name__ == '__main__':
    main()
