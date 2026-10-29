#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""
Radtel RT-950 Pro .BTF firmware image tool.

Converts a raw firmware binary, linked at 0x08003000, into the .BTF format
accepted by the vendor bootloader (and thus by the official RT-950 Pro update
tool), or extracts the raw binary back from a .BTF file.

The format has been reverse engineered in
https://github.com/jaymart1983/Radtel-RT950-Pro-Firmware (tools/encrypt_btf.py):

  BTF[0x000:0x400]  first 1kB of the firmware, plaintext (vector table and the
                    model signature at offset 0x3E0)
  BTF[0x400:0x800]  key block: 16-byte key followed by its expansion, not
                    written to flash by the bootloader
  BTF[0x800:...]    remainder of the firmware, from binary offset 0x400

From offset 0x800 onwards each byte is XORed with the 128-byte expanded key,
indexed by the byte offset in the .BTF file. Bytes equal to 0x00 or 0xFF, or
which would become 0x00 or 0xFF, are left unchanged.

The bootloader does not commit the last block it receives: the firmware is
padded with 0xFF to a whole number of 1kB blocks and an extra block of 0xFF is
appended.
"""

import argparse
import struct
import sys

BLOCK_SIZE = 0x400
KEY_OFFSET = 0x400
KEY_LENGTH = 16
ENCRYPT_START = 0x800
EXPANDED_KEY_LEN = 128

FLASH_BASE = 0x08003000
FLASH_END = 0x08100000
SRAM_BASE = 0x20000000
SRAM_END = 0x20018000

MODEL_STRING = b"RT-950      "
MODEL_OFFSETS = (0x3E0, 0x7E0)


def rol8(value):
    return ((value << 1) | (value >> 7)) & 0xFF


def ror8(value):
    return ((value >> 1) | (value << 7)) & 0xFF


def expand_key(key):
    """Expand the 16-byte key into 8 blocks of 16 bytes, each one obtained by
    rotating the bytes of the previous block: left for bytes 0-7, right for
    bytes 8-15."""
    blocks = [bytes(key)]
    for _ in range(EXPANDED_KEY_LEN // KEY_LENGTH - 1):
        prev = blocks[-1]
        blocks.append(bytes(rol8(prev[i]) if i < 8 else ror8(prev[i])
                            for i in range(KEY_LENGTH)))
    return b"".join(blocks)


def xor_payload(data, expanded_key):
    """Apply the (symmetric) BTF scrambling from ENCRYPT_START onwards."""
    out = bytearray(data)
    for i in range(ENCRYPT_START, len(out)):
        value = out[i]
        if value in (0x00, 0xFF):
            continue
        xored = value ^ expanded_key[i % EXPANDED_KEY_LEN]
        if xored in (0x00, 0xFF):
            continue
        out[i] = xored
    return out


def check_firmware(firmware):
    """Sanity checks on the raw image, the vendor bootloader performs only
    minimal ones and an invalid image may leave the radio unbootable."""
    errors = []

    if len(firmware) < ENCRYPT_START:
        errors.append(f"image too small ({len(firmware)} bytes)")
        return errors

    if len(firmware) > FLASH_END - FLASH_BASE:
        errors.append(f"image too large ({len(firmware)} bytes, maximum "
                      f"{FLASH_END - FLASH_BASE})")

    sp, reset = struct.unpack_from("<II", firmware, 0)
    if not (SRAM_BASE < sp <= SRAM_END) or (sp & 0x2FFE0000) != SRAM_BASE:
        errors.append(f"invalid initial stack pointer 0x{sp:08X}")

    if not (reset & 1) or not (FLASH_BASE <= (reset & ~1) < FLASH_END):
        errors.append(f"invalid reset vector 0x{reset:08X}, the image must be "
                      f"linked at 0x{FLASH_BASE:08X}")

    for offset in MODEL_OFFSETS:
        found = bytes(firmware[offset:offset + len(MODEL_STRING)])
        if found != MODEL_STRING:
            errors.append(f"model signature not found at offset 0x{offset:X} "
                          f"(found {found!r})")

    return errors


def pad_firmware(firmware):
    rem = len(firmware) % BLOCK_SIZE
    if rem:
        firmware += b"\xFF" * (BLOCK_SIZE - rem)
    # Sacrificial block, the bootloader does not commit the last one
    return firmware + b"\xFF" * BLOCK_SIZE


def build_btf(firmware, key):
    expanded = expand_key(key)

    key_block = bytearray(BLOCK_SIZE)
    key_block[:KEY_LENGTH] = key
    key_block[KEY_LENGTH:KEY_LENGTH + EXPANDED_KEY_LEN] = expanded

    firmware = pad_firmware(bytes(firmware))
    btf = firmware[:BLOCK_SIZE] + bytes(key_block) + firmware[BLOCK_SIZE:]
    return xor_payload(btf, expanded)


def extract_firmware(btf, key=None):
    if len(btf) < ENCRYPT_START:
        raise ValueError("file too small to be a .BTF image")

    if key is None:
        key = bytes(btf[KEY_OFFSET:KEY_OFFSET + KEY_LENGTH])

    plain = xor_payload(btf, expand_key(key))
    return plain[:BLOCK_SIZE] + plain[ENCRYPT_START:]


def parse_key(text):
    try:
        key = bytes.fromhex(text)
    except ValueError:
        raise argparse.ArgumentTypeError(f"invalid hex key: {text}")
    if len(key) != KEY_LENGTH:
        raise argparse.ArgumentTypeError(
            f"key must be {KEY_LENGTH} bytes ({2 * KEY_LENGTH} hex digits)")
    return key


def main():
    parser = argparse.ArgumentParser(
        description="Radtel RT-950 Pro .BTF firmware image tool")
    parser.add_argument("input", help="input file (.bin, or .BTF with "
                                      "--decrypt)")
    parser.add_argument("output", help="output file (.BTF, or .bin with "
                                       "--decrypt)")
    parser.add_argument("--key", type=parse_key, default=None,
                        help="16-byte key as 32 hex digits (default: all "
                             "zeros). When decrypting, the key stored in the "
                             "file is used unless one is given.")
    parser.add_argument("--decrypt", action="store_true",
                        help="extract the raw firmware from a .BTF image")
    parser.add_argument("--no-check", action="store_true",
                        help="skip the sanity checks on the firmware image")
    args = parser.parse_args()

    with open(args.input, "rb") as f:
        data = f.read()

    if args.decrypt:
        result = extract_firmware(data, args.key)
    else:
        if not args.no_check:
            errors = check_firmware(data)
            if errors:
                for error in errors:
                    print(f"error: {args.input}: {error}", file=sys.stderr)
                sys.exit(1)
        key = args.key if args.key is not None else bytes(KEY_LENGTH)
        result = build_btf(data, key)

    with open(args.output, "wb") as f:
        f.write(result)

    print(f"{args.output}: {len(result)} bytes")


if __name__ == "__main__":
    main()
