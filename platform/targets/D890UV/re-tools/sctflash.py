#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# AnyTone AT-D890UV reverse-engineering helper, see ../NOTES.md (3.7).
# SCT3288 firmware update, reimplementing AnyTone's/Sicomm's SCT_PORT "Flash
# Update" (SCT_Device.dll, SCT3252::DownLoadCustomApplicationHexFileToFlash,
# platform SCT3288 = version type 2).
#
#   sctflash.py <image.hex> [--max N] [--dry-run | --rehearse | --run] [--show N]
#
# --dry-run   parse the image like the tool does and print the packet stream
# --rehearse  open programming mode on the radio and run every step up to the
#             first erase (parity on, then off again): nothing is erased
# --run       the real update: erases and writes the chip's flash
# The radio must run OpenRTX with the monitor "sx" command; the radio driver's
# use of the chip is disabled first (sctReady).
import sys, os, time, argparse, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))

ap = argparse.ArgumentParser()
ap.add_argument('hex')
ap.add_argument('--max', type=int, default=1280,
                help='write packet payload limit (tool default 1280)')
ap.add_argument('--dry-run', action='store_true')
ap.add_argument('--rehearse', action='store_true')
ap.add_argument('--run', action='store_true')
ap.add_argument('--elf', default=os.path.join(HERE, '../../../../build_cm4/openrtx_d890uv'),
                help='firmware running on the radio, to find the radio driver flag sctReady')
ap.add_argument('--port', default='/dev/ttyACM0')
ap.add_argument('--write-timeout', type=float, default=2.0, help='seconds per write packet')
ap.add_argument('--show', type=int, default=4, help='write packets shown at each end')
a = ap.parse_args()

HEAD = b'\x84\xa9'
MASTER = 0x61
TYPE_CONTROL, TYPE_PROGRAM = 0x00, 0x03
F_PARITY_ENABLE, F_INIT_FLASH, F_WRITE_FLASH, F_PARITY = 0x16, 0x93, 0x94, 0x2F
# SCT_PORT sets version type 2 (DMR_T2) when "SCT3288" is selected; TRIGGERS
# below are the region addresses for that version type.


def packet(ptype, fields, parity):
    """CreatCmd + SendCmd: header, optional PARITY field, even length."""
    body = bytes(fields)
    if parity:
        body += bytes([F_PARITY, 0])
    p = bytearray(HEAD + bytes([MASTER]) + len(body).to_bytes(2, 'big') + bytes([ptype]) + body)
    if parity:
        x = 0
        for b in p[3:-1]:       # length MSB .. the 0x2F byte (GenerateParity)
            x ^= b
        p[-1] = x
    if len(p) % 2:
        p.append(0)
    return bytes(p)


def init_flash(start, region, parity):
    return packet(TYPE_CONTROL, [F_INIT_FLASH, (1 if start else 0) | (region << 1)], parity)


def write_flash(addr, data, parity):
    hdr = [F_WRITE_FLASH, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF, min(len(data), 255)]
    return packet(TYPE_PROGRAM, hdr + list(data), parity)


# Region triggers of DownLoadCustomApplicationHexFileToFlash for version type 2:
# absolute address of a data record -> (InitFlash region code, tool message).
# For the User region the code is 7 on an SCT3917, 1 otherwise.
TRIGGERS = {
    233472: (2, 'DownLoad SystemFlash Code'),
    256: (1, 'DownLoad UserFlash Code'),
    139264: (5, 'DownLoad Vocoder1 Code'),
    262144: (6, 'DownLoad Vocoder2 Code'),
    405504: (3, 'DownLoad Vocoder3 Code'),
    217088: (10, 'DownLoad Vocoder4 Code'),
    487424: (15, 'DownLoad Vocoder5 Code / User2Flash Code'),
    339968: (12, 'DownLoad Vocoder6 Code or User3 Flash'),
    352256: (13, 'DownLoad Vocoder7 Code'),
    376832: (13, 'DownLoad Vocoder8 Code'),
}
SYSTEM_REGION = 2


# --- build the command stream exactly as the tool does ------------------------
steps = []          # (description, packet bytes, expected answer field, timeout s)
steps.append(('ReadEmptyHPI: drain pending data', None, None, 0))
pending = bytearray()
waddr = 0
base = 0
parity = False      # SCT3252._isParity
started = False
regions = []
problems = []


def setup_parity(on):
    global parity
    steps.append((f'SetupParityCheck({int(on)})',
                  packet(TYPE_CONTROL, [F_PARITY_ENABLE, 1 if on else 0], parity), F_PARITY_ENABLE, 0.3))
    parity = on


def init_flash_step(start, region, msg):
    # InitFlash(flag, region): SetupParityCheck(1), 50 ms, then field 0x93
    setup_parity(True)
    steps.append((f'InitFlash({int(start)}, {region}) {msg}', init_flash(start, region, parity), F_INIT_FLASH, 5))


def flush():
    global pending, waddr
    if pending:
        steps.append((f'WritFlash {waddr:#08x} len {len(pending)}',
                      write_flash(waddr, pending, True), F_WRITE_FLASH, a.write_timeout))
        waddr += len(pending)
        pending = bytearray()


for lineno, line in enumerate(open(a.hex), 1):
    line = line.strip()
    if not line.startswith(':'):
        continue
    rec = bytes.fromhex(line[1:])
    if (sum(rec) & 0xFF) != 0:
        problems.append(f'line {lineno}: bad checksum')
        continue
    n, addr16, rtype, data = rec[0], (rec[1] << 8) | rec[2], rec[3], rec[4:4 + rec[0]]
    if rtype == 4 and n == 0:
        # section boundary as the tool reads it: flush, base = record address
        flush()
        base = addr16
        continue
    if rtype == 1:
        flush()
        break
    if rtype != 0:
        problems.append(f'line {lineno}: record type {rtype} with data (not handled by the tool path we follow)')
        continue
    addr = (base << 16) | addr16
    if len(pending) + n > a.max:
        flush()
    if addr in TRIGGERS:
        code, msg = TRIGGERS[addr]
        if code == SYSTEM_REGION:
            problems.append(f'line {lineno}: SYSTEM region at {addr:#x} (loader) would be rewritten')
        started = True
        waddr = addr
        regions.append((addr, code, msg))
        init_flash_step(True, code, msg)
    if not started:
        problems.append(f'line {lineno}: data before any region trigger')
    pending += data

init_flash_step(False, 0, 'end of download')
setup_parity(False)     # back to the state before the download

# --- report --------------------------------------------------------------------
writes = [s for s in steps if s[0].startswith('WritFlash')]
total = sum(int(s[0].split()[-1]) for s in writes)
first_w = int(writes[0][0].split()[1], 16) if writes else 0
last_w = writes[-1] if writes else None
print(f'image {a.hex}')
print(f'  data bytes {total}, write packets {len(writes)} (payload limit {a.max})')
if writes:
    end = int(last_w[0].split()[1], 16) + int(last_w[0].split()[-1])
    print(f'  flash range written {first_w:#08x} - {end:#08x}')
print(f'  problems: {problems if problems else "none"}')
print('  regions (address, InitFlash code):')
for ad, code, msg in regions:
    print(f'    {ad:#08x} code {code:2d}  {msg}')
# write ranges per region
spans = []
cur = None
for d, pk, f, t in steps:
    if d.startswith('InitFlash'):
        if cur and cur[1] is not None: spans.append(cur)
        cur = [d.split(' ', 1)[1] if d.startswith('InitFlash(1') else 'end', None, 0]
    elif d.startswith('WritFlash'):
        a0 = int(d.split()[1], 16); ln = int(d.split()[-1])
        if cur[1] is None: cur[1] = a0
        if cur[2] and a0 != cur[2]: problems.append(f'non-contiguous write at {a0:#x} in {cur[0]}')
        cur[2] = a0 + ln
for name, lo, hi in spans:
    print(f'    writes {lo:#08x}-{hi:#08x} ({hi - lo:6d} bytes)  {name}')
overl = [(x, y) for i, x in enumerate(spans) for y in spans[i + 1:] if x[1] < y[2] and y[1] < x[2]]
print(f'  overlapping regions: {overl if overl else "none"}')
print()
for i, (desc, pkt, field, tmo) in enumerate(steps):
    if desc.startswith('WritFlash'):
        k = writes.index((desc, pkt, field, tmo))
        if a.show <= k < len(writes) - a.show:
            if k == a.show:
                print(f'  ... {len(writes) - 2 * a.show} more write packets ...')
            continue
    hexs = pkt.hex(' ') if pkt else ''
    if len(hexs) > 110:
        hexs = hexs[:60] + ' ... ' + hexs[-30:] + f'  ({len(pkt)} bytes)'
    exp = f'expect field {field:#04x} code 0' if field else ''
    print(f'{i:5d} {desc:52s} {exp:26s} {hexs}')

if not (a.rehearse or a.run):
    sys.exit(0)
if problems:
    sys.exit('refusing to send: the image has problems')

# --- sending ---------------------------------------------------------------------
sys.path.insert(0, HERE)
from monitor import Monitor

m = Monitor(a.port)
m.p.timeout = 30


def split_frames(raw):
    frames, i = [], 0
    while i + 6 <= len(raw):
        if raw[i:i + 3] != b'\x84\xa9\x61':
            i += 1
            continue
        n = (raw[i + 3] << 8) | raw[i + 4]
        frames.append(raw[i:i + 6 + n])
        i += 6 + n + ((6 + n) % 2)
    return frames


def exchange(pkt, timeout_ms):
    line = m.cmd(f'sx {pkt.hex()} {timeout_ms:x}')[0]
    raw = b'' if line == 'RX -' else bytes.fromhex(line[3:])
    return split_frames(raw)


def check(frame, with_parity):
    """(field, code) of an answer; parity checked and removed like RemoveParity."""
    body = frame[6:]
    if with_parity:
        if len(body) < 2 or body[-2] != F_PARITY:
            return None, 'no parity field'
        x = 0
        for b in frame[3:-1]:
            x ^= b
        if x != frame[-1]:
            return None, f'parity {frame[-1]:#04x}, expected {x:#04x}'
        body = body[:-2]
    if len(body) < 2:
        return None, 'short answer'
    return body[0], body[1]


def do_step(pkt, field, tmo, parity_now):
    """SendCmdAndVerifyRecRespond: two attempts, resend on ACK 3 or 6."""
    reason = ''
    for attempt in range(2):
        frames = exchange(pkt, int(tmo * 1000))
        if not frames:
            reason = 'no answer'
            continue
        f, code = check(frames[0], parity_now)
        if f is None:
            reason = code
            continue
        if f == field and code == 0:
            return True, frames
        if f == 0x17 and code in (3, 6):
            reason = f'ACK {code}, resending'
            continue
        return False, f'answer field {f:#04x} code {code}'
    return False, reason


sym = [l.split() for l in subprocess.run(['arm-none-eabi-nm', a.elf], capture_output=True,
                                          text=True).stdout.splitlines()]
ready = [x[0] for x in sym if len(x) == 3 and x[2] == 'sctReady']
if len(ready) != 1:
    sys.exit('sctReady not found in the firmware image')
print('\nopening programming mode (sctReady at', ready[0] + ')')
m.cmd(f'wb {ready[0]} 0')
m.cmd('sa 0')
m.cmd('so')
time.sleep(0.5)
m.cmd('si')
frames = exchange(bytes.fromhex('74a901039000'), 3500)
print('  interface word (mode 1):', [f.hex() for f in frames])
if not frames or frames[0][6:8] != b'\x17\x01':
    sys.exit('chip did not report ready in programming mode; nothing sent')
for q in (0x30, 0x32):
    fr = exchange(packet(0x23, [q], False), 500)
    print(f'  query {q:#04x}:', fr[0][8:].decode('ascii', 'replace') if fr else '-')

t0 = time.time()
for i, (desc, pkt, field, tmo) in enumerate(steps):
    if pkt is None:
        continue
    if a.rehearse and desc.startswith('InitFlash'):
        print(f'{i:5d} STOP before {desc} (rehearsal)')
        fr = exchange(packet(TYPE_CONTROL, [F_PARITY_ENABLE, 0], True), 500)
        print('      parity off:', [f.hex() for f in fr])
        break
    # answers carry parity when the packet does (PARITY field last)
    blen = (pkt[3] << 8) | pkt[4]
    with_parity = (blen >= 2) and (pkt[6 + blen - 2] == F_PARITY)
    if desc.startswith('SetupParityCheck'):
        frames = exchange(pkt, 300)
        print(f'{i:5d} {desc}: {[f.hex() for f in frames]}')
        continue
    ok, info = do_step(pkt, field, tmo, with_parity)
    if not ok:
        print(f'{i:5d} FAILED {desc}: {info}')
        sys.exit(f'stopped at step {i}; the chip stays in programming mode')
    if not desc.startswith('WritFlash') or i % 20 == 0:
        print(f'{i:5d} ok {desc}  {time.time() - t0:.1f}s', flush=True)
print(f'done in {time.time() - t0:.1f}s')
