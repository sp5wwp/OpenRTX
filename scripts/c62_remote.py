#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Remote control for C62_REMOTE builds, over the programming cable.

Commands go to the radio as text lines; replies come back as control
packets of the baseband dump (magic A5 5A C3 3D), between its sample packets
(A5 5A C3 3C). See platform/drivers/audio/c62_remote.c for the commands.

  c62_remote.py -p /dev/ttyUSB0 ping s "m t" "f 438275000"
  c62_remote.py -p /dev/ttyUSB0 --watch 1000     # status every second

As a module:
  r = Remote("/dev/ttyUSB0")
  r.cmd("d 0")
  st = r.status()        # dict: e, cfo, crc, fr, bad, ...
"""

import argparse
import queue
import struct
import sys
import threading
import time

import serial

MAGIC_SAMPLES = b"\xa5\x5a\xc3\x3c"
MAGIC_CTRL = b"\xa5\x5a\xc3\x3d"
HEADER = struct.Struct("<4sBBHIHHHhH")  # 22 bytes


def crc16(data):
    """CRC-16/CCITT-FALSE."""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


def parse_status(text):
    """'S t=1 m=4 ...' -> dict of ints (strings where not numeric)."""
    out = {}
    for item in text.split()[1:]:
        k, _, v = item.partition("=")
        try:
            out[k] = int(v)
        except ValueError:
            out[k] = v
    return out


class Remote:
    def __init__(self, port, baud=115200, on_samples=None):
        self.ser = serial.Serial(port, baud, timeout=0.05)
        self.replies = queue.Queue()
        self.on_samples = on_samples  # f(header tuple, payload bytes)
        self.packets = 0
        self.crc_errors = 0
        self._buf = bytearray()
        self._stop = False
        self._rx = threading.Thread(target=self._reader, daemon=True)
        self._rx.start()

    def close(self):
        self._stop = True
        self._rx.join()
        self.ser.close()

    def _reader(self):
        while not self._stop:
            data = self.ser.read(4096)
            if data:
                self._buf += data
                self._parse()

    def _parse(self):
        buf = self._buf
        while True:
            i = buf.find(b"\xa5\x5a\xc3")
            if i < 0:
                del buf[:max(0, len(buf) - 2)]
                return
            del buf[:i]
            if len(buf) < 5:
                return
            if buf[:4] == MAGIC_CTRL:
                n = buf[4]
                if len(buf) < 5 + n + 2:
                    return
                body = bytes(buf[4:5 + n])
                (crc,) = struct.unpack_from("<H", buf, 5 + n)
                if crc == crc16(body):
                    self.replies.put(body[1:].decode("ascii", "replace"))
                    del buf[:5 + n + 2]
                else:
                    self.crc_errors += 1
                    del buf[:1]
            elif buf[:4] == MAGIC_SAMPLES:
                if len(buf) < HEADER.size:
                    return
                h = HEADER.unpack_from(buf)
                size = HEADER.size + h[9] + 2
                if h[9] > 4096:
                    del buf[:1]
                    continue
                if len(buf) < size:
                    return
                (crc,) = struct.unpack_from("<H", buf, size - 2)
                if crc == crc16(bytes(buf[4:size - 2])):
                    self.packets += 1
                    if self.on_samples:
                        self.on_samples(h, bytes(buf[HEADER.size:size - 2]))
                    del buf[:size]
                else:
                    self.crc_errors += 1
                    del buf[:1]
            else:
                del buf[:1]

    def send(self, line):
        """Send one command line, paced so the radio's polling keeps up."""
        for ch in (line + "\n").encode():
            self.ser.write(bytes([ch]))
            self.ser.flush()
            time.sleep(0.002)

    def cmd(self, line, timeout=3.0, expect=None, tries=3):
        """Send a command, return the first reply starting with expect
        (default: the command's first word, or E for an error). The radio
        polls its UART: a busy moment can lose a character, so retry."""
        for i in range(tries):
            try:
                return self._cmd(line, timeout / tries if i else timeout,
                                 expect)
            except TimeoutError:
                if i == tries - 1:
                    raise

    def _cmd(self, line, timeout, expect):
        want = expect or {"ping": "pong", "s": "S "}.get(line.split()[0],
                                                         line.split()[0])
        self.send(line)
        end = time.time() + timeout
        while time.time() < end:
            try:
                r = self.replies.get(timeout=max(0.01, end - time.time()))
            except queue.Empty:
                break
            if r.startswith(want) or r.startswith("E"):
                return r
        raise TimeoutError(f"no reply to {line!r}")

    def status(self, timeout=3.0):
        return parse_status(self.cmd("s", timeout, expect="S "))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-p", "--port", default="/dev/ttyUSB0")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--watch", type=int, default=0,
                    help="print the status every N ms, Ctrl-C to stop")
    ap.add_argument("commands", nargs="*")
    a = ap.parse_args()

    r = Remote(a.port, a.baud)
    try:
        for c in a.commands:
            try:
                print(r.cmd(c))
            except TimeoutError as e:
                print(e, file=sys.stderr)
            # Commands with several replies (p): drain the rest
            time.sleep(0.2)
            while not r.replies.empty():
                print(r.replies.get())
        if a.watch:
            r.cmd(f"a {a.watch}")
            try:
                while True:
                    print(r.replies.get(), flush=True)
            except KeyboardInterrupt:
                r.cmd("a 0")
    finally:
        r.close()


if __name__ == "__main__":
    main()
