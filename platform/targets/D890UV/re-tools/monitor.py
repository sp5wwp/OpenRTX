#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# AnyTone AT-D890UV OpenRTX debug monitor client, see ../NOTES.md (3.2).
#   monitor.py [--port /dev/ttyACM0] <command line>     run one command
#   monitor.py nand-dump <addr> <len> <file>            NAND to a binary file
#   monitor.py mem-dump <addr> <len> <file>             memory to a binary file
# Also usable as a module: Monitor(port).cmd('xr a 2'), .nand(addr, len).
import sys, time, serial

class Monitor:
    def __init__(self, port='/dev/ttyACM0'):
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
            l = l.decode().strip()
            if l == 'OK':
                return out
            if l.startswith('ERR'):
                raise RuntimeError(f'{line!r}: {l}')
            out.append(l)

    def _read(self, c, addr, length):
        data = bytearray()
        while length > 0:
            n = min(length, 4096)
            for l in self.cmd(f'{c} {addr:x} {n:x}'):
                data += bytes.fromhex(l.split()[1])
            addr += n; length -= n
        return bytes(data)

    def nand(self, addr, length): return self._read('nr', addr, length)
    def mem(self, addr, length): return self._read('rb', addr, length)
    def wx(self, chip, reg): return int(self.cmd(f'xr {chip} {reg:x}')[0], 16)
    def wxw(self, chip, reg, val): self.cmd(f'xw {chip} {reg:x} {val:x}')

if __name__ == '__main__':
    a = sys.argv[1:]; port = '/dev/ttyACM0'
    if a[:1] == ['--port']: port = a[1]; a = a[2:]
    m = Monitor(port)
    if a[0] in ('nand-dump', 'mem-dump'):
        f = m.nand if a[0] == 'nand-dump' else m.mem
        open(a[3], 'wb').write(f(int(a[1], 16), int(a[2], 16)))
    else:
        print('\n'.join(m.cmd(' '.join(a))))
