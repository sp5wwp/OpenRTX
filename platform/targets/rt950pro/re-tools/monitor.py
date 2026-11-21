#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Radtel RT-950 Pro OpenRTX debug monitor client, see ../monitor.c.
#   monitor.py [--port /dev/ttyUSB0] <command line>     run one command
#   monitor.py mem-dump <addr> <len> <file>             memory to a binary file
# Also usable as a module: Monitor(port).cmd('ping'), .mem(addr, len).
# The port is shared with the Zephyr log: lines starting with '[' are skipped.
import sys, time, serial

class Monitor:
    def __init__(self, port='/dev/ttyUSB0'):
        self.p = serial.Serial(port, 115200, timeout=2)
        time.sleep(0.1)
        self.p.reset_input_buffer()

    def cmd(self, line):
        """Run a command, return its output lines; raise on ERR."""
        self.p.write((line + '\n').encode())
        out = []
        while True:
            l = self.p.readline()
            if not l:
                raise TimeoutError(f'no reply to {line!r}')
            l = l.decode(errors='replace').strip()
            if not l or l.startswith('['):
                continue
            if l == 'OK':
                return out
            if l.startswith('ERR'):
                raise RuntimeError(f'{line!r}: {l}')
            out.append(l)

    def mem(self, addr, length):
        data = bytearray()
        while length > 0:
            n = min(length, 4096)
            for l in self.cmd(f'rb {addr:x} {n:x}'):
                data += bytes.fromhex(l.split()[1])
            addr += n; length -= n
        return bytes(data)

if __name__ == '__main__':
    a = sys.argv[1:]; port = '/dev/ttyUSB0'
    if a[:1] == ['--port']: port = a[1]; a = a[2:]
    m = Monitor(port)
    if a[0] == 'mem-dump':
        open(a[3], 'wb').write(m.mem(int(a[1], 16), int(a[2], 16)))
    else:
        print('\n'.join(m.cmd(' '.join(a))))
