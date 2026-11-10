#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""
Wrap a raw firmware binary into the AnyTone update file set (.spi, .CDI and
.CDD), as accepted by the vendor CPS and by flash_anytone.py
(https://github.com/sp5wwp/anytone-flasher).

The layout follows the D890UV_V1.05_20260521 update files:
  .spi  51 bytes: 0x20 header, file count, image size, model and hardware
        version strings
  .CDI  one 278-byte directory entry: file name, load address, size, offset
  .CDD  the raw image
"""

import argparse
import struct
import sys


def make_spi(model, hwver, size):
    spi = struct.pack('<IHII', 0x20, 1, size, 0)
    spi += model.ljust(8).encode() + hwver.encode()
    spi += model.ljust(16).encode() + hwver.encode()
    spi += b'    \x00'
    assert len(spi) == 51
    return spi


def make_cdi(model, addr, size):
    name = (model + '_MAIN.bin').encode() + b'\x00'
    cdi = name.ljust(256, b' ')
    # Load address, size, offset in the .CDD, then a flags word set to 1
    cdi += struct.pack('<IIIHHIH', addr, size, 0, 0, 1, 0, 0)
    assert len(cdi) == 278
    return cdi


def main():
    ap = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    ap.add_argument('input', help='raw binary image')
    ap.add_argument('output', help='output base name, without extension')
    ap.add_argument('--model', required=True, help='radio model, e.g. D890UV')
    ap.add_argument('--hwver', required=True, help='hardware version, e.g. V100')
    ap.add_argument('--addr', required=True, type=lambda x: int(x, 0),
                    help='load address, e.g. 0x0800C000')
    a = ap.parse_args()

    if len(a.model) > 8 or len(a.hwver) != 4:
        sys.exit('model must be at most 8 characters, hwver exactly 4')

    img = open(a.input, 'rb').read()
    open(a.output + '.spi', 'wb').write(make_spi(a.model, a.hwver, len(img)))
    open(a.output + '.CDI', 'wb').write(make_cdi(a.model, a.addr, len(img)))
    open(a.output + '.CDD', 'wb').write(img)


if __name__ == '__main__':
    main()
