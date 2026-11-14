#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# AnyTone AT-D890UV stock firmware reverse-engineering helper, see ../NOTES.md.
# Run the whole stock firmware (start-up and main loop) in an emulator:
#   fullemu.py <firmware.CDD> [options]
# - NAND reads (0x08037A9C) are served from the radio itself through the
#   OpenRTX debug monitor (--port), cached in --cache; writes and erases are
#   logged and dropped.
# - The SCT3288 is a fake chip answering like the real one did on the radio.
# - WX4818N register writes are logged; reads return the last value written.
# - Peripheral status registers are modelled enough for HAL polling loops.
# - SysTick and the TIM5-7 interrupts run every --slice instructions (1 ms);
#   uC/OS-II context switches are done here (see NOTES.md 3.6).
# - PTT (PA10) can be pressed over a time window (--ptt).
# Output: one line per event (SCT packets, WX writes, selected GPIOs, NAND
# writes), progress lines, and a report when the code gets stuck in a loop.
import sys, os, struct, pickle, time, argparse, collections
from unicorn import *
from unicorn.arm_const import *

ap = argparse.ArgumentParser()
ap.add_argument('fw')
ap.add_argument('--port', default='/dev/ttyACM0')
ap.add_argument('--cache', default='nandcache.pkl')
ap.add_argument('--offline', action='store_true', help='do not fetch NAND from the radio')
ap.add_argument('--slice', type=int, default=200000, help='instructions per SysTick')
ap.add_argument('--ms', type=int, default=3000, help='emulated milliseconds to run')
ap.add_argument('--quiet-wx', action='store_true', help='log WX writes only while PTT is pressed')
ap.add_argument('--watch', default='', help='hexaddr,... log r0 when reached')
ap.add_argument('--trace', default='', help='start:end, ms window logging function entries')
ap.add_argument('--funcs', help='funcs.pkl, function start addresses (for --trace)')
ap.add_argument('--watchmem', default='', help='hexaddr,... log writes to these bytes')
ap.add_argument('--peek', default='', help='hexaddr,... print bytes after init and at the end')
ap.add_argument('--batt', type=int, default=10600,
                help='battery ADC sample written every ms to 0x20001D36 (ADC DMA buffer); '
                     '10600 is about 7.5 V, at or below 6.0 V stock blocks TX (0x08039024)')
ap.add_argument('--periphw', default='', help='start:end, ms window logging peripheral writes')
ap.add_argument('--dump', default='', help='hexaddr:len,... print memory at the end')
ap.add_argument('--watchread', default='', help='hexaddr,... log reads of these bytes (pc counted)')
ap.add_argument('--mic', action='store_true', help='1 kHz sine in the mic ADC slot 0x20001D3A')
ap.add_argument('--count', default='', help='hexaddr,... functions whose calls are counted')
ap.add_argument('--ptt', default='', help='start:end, ms with PTT (PA10) pressed')
ap.add_argument('--poke', default='', help='hexaddr:hexbytes,... after start-up init')
ap.add_argument('--boot', help='bootloader image read from the radio (holds the ID record at 0x0800A000)')
ap.add_argument('--uid', help='MCU unique ID read from the radio (12 bytes at 0x0BFA0700)')
a = ap.parse_args()

B = 0x0800C000
fw = open(a.fw, 'rb').read()
MAIN, SCATTER, SYSTICK = 0x0813EF78, 0x080101D0, 0x081209C0

# --- stock functions handled in Python ---------------------------------------
NAND_READ, NAND_WRITE, NAND_ERASE = 0x08037A9C, 0x08037E78, 0x08037840
NAND_TXBYTES, NAND_RXBYTES = 0x08037E48, 0x08037D84   # raw bytes (buf, len)
NAND_ID = b'\x98\xed\x51'                            # as read on the radio
WX_WRITE, WX_READ = 0x080E2F64, 0x080E2CAC
SCT_WR, SCT_RD, SCT_SEND = 0x0810387C, 0x08103838, 0x0810A674
GPIO_READ, GPIO_WRITE, GPIO_INIT = 0x0804FBA8, 0x0804FBD8, 0x0804F7D0
DELAYS = (0x0811EC64, 0x080F0600, 0x0811ECE0)   # ms, ms, us (SysTick VAL)
ERROR_HANDLER = 0x08034AC8   # endless loop called on HAL errors
GPIOA, GPIOB, GPIOC, GPIOD, GPIOE = (0x42020000 + i * 0x400 for i in range(5))
PORT = {GPIOA: 'PA', GPIOB: 'PB', GPIOC: 'PC', GPIOD: 'PD', GPIOE: 'PE'}
# Pins worth logging: audio routing, power, TX, digital board, LEDs
LOGPINS = {GPIOA: {1, 15}, GPIOB: set(), GPIOC: {3, 6, 7},
           GPIOD: {0, 1, 2, 8, 9, 10}, GPIOE: {0, 1, 3, 4, 5}}

mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
for base, size in ((0x0, 0x10000), (0x08000000, 0x200000), (0x0BF80000, 0x40000),
                   (0x20000000, 0x100000), (0x28000000, 0x10000),
                   (0x40000000, 0x20000000), (0xE0000000, 0x100000)):
    mu.mem_map(base, size)
mu.mem_write(B, fw)
# The firmware checks the ID record at 0x0800A000 against the MCU unique ID
# (0x080215C0, 0x08063780) and stops in an endless loop if they disagree.
if a.boot:
    mu.mem_write(0x08000000, open(a.boot, 'rb').read()[:0xC000])
# RTC backup register 0: 0x5050 = clock set, 0x6060 = ask the user for the
# time at start-up, anything else = initialise the RTC (0x08062192)
mu.mem_write(0x46007D00, struct.pack('<I', 0x5050))
if a.uid:
    mu.mem_write(0x0BFA0700, open(a.uid, 'rb').read()[:12])
RET = 0x20090000
mu.mem_write(RET, b'\x00\xbf\xfe\xe7')

def out(s):
    print(s, flush=True)

# --- NAND, from the radio ----------------------------------------------------
cache = pickle.load(open(a.cache, 'rb')) if os.path.exists(a.cache) else {}
mon = None
def nandPage(page):
    global mon
    if page in cache:
        return cache[page]
    if a.offline:
        return b'\xff' * 4096
    if mon is None:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from monitor import Monitor
        mon = Monitor(a.port); mon.p.timeout = 10
    data = mon.nand(page << 12, 4096)
    cache[page] = data
    pickle.dump(cache, open(a.cache, 'wb'))
    return data

def nandRead(addr, n):
    buf = bytearray()
    while n > 0:
        page, col = addr >> 12, addr & 0xFFF
        chunk = min(n, 4096 - col)
        buf += nandPage(page)[col:col + chunk]
        addr += chunk; n -= chunk
    return bytes(buf)

# --- fake SCT3288 ------------------------------------------------------------
host = bytearray(); toHost = bytearray()
def sctReply(t, body):
    out(f'SCT< type={t:#04x} {body.hex(" ")}')
    toHost.extend(b'\x84\xa9\x61' + struct.pack('>H', len(body)) + bytes([t]) + body)

def sctHostByte(b):
    host.append(b)
    while len(host) >= 3 and host[:3] != b'\x84\xa9\x61':
        host.pop(0)
    if len(host) >= 6:
        n = 6 + ((host[3] << 8) | host[4])
        if len(host) >= n:
            p = bytes(host[:n]); del host[:n]
            if n % 2 and host[:1] == b'\x00':  # 16-bit padding byte
                host.pop(0)
            t, body = p[5], p[6:]
            out(f'SCT> type={t:#04x} {body.hex(" ")}')
            if t & 0x40:                       # codec bypass
                sctReply(0x40, b'\x17\x00')
            elif body[:2] == b'\x10\x10':            # analog vocoder: the real
                sctReply(0x20, b'\x17\x01')         # chip resets (17 01),
                sctReply(0x00, b'\x10\x00')         # then accepts
            elif body[:1] == b'\x33' and t & 0x20:  # status, as the real chip
                sctReply(0x23, b'\x33\x00\x00\x21\x00\x06\x78\x05\x00\x00\x00')
            elif body[:1] == b'\x2d':
                sctReply(0x00, b'\x2d\x00')
            elif body[:1] == b'\x1a':
                sctReply(t, b'\x17\x11')       # as the real chip answered
            elif t & 0x20:                     # read request
                if body[:1] == b'\x32':
                    sctReply(0x20, b'\x32\x0aV3.01.01A3')
                else:
                    sctReply(0x20, body[:1] + b'\x00')
            else:
                sctReply(t, body[:1] + b'\x00')

# --- WX4818N -----------------------------------------------------------------
wxRegs = collections.defaultdict(int)
wxRegs[(0, 0x57)] = wxRegs[(1, 0x57)] = 0xA888

# --- hooks -------------------------------------------------------------------
tick = [0]
pinState = {}
idrReads = collections.Counter()
gpioReads = collections.Counter()
gpioLast = {}
pttOn = [False]
nandCmd = [0]
stopNow = [False]
def ret(uc, value):
    uc.reg_write(UC_ARM_REG_R0, value)
    uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

def hook(uc, addr, size, _):
    r = [uc.reg_read(x) for x in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)]
    lr = uc.reg_read(UC_ARM_REG_LR)
    if addr == RET:
        uc.emu_stop(); return
    if addr == NAND_READ:
        for x in filter(None, a.watchmem.split(',')):
            if r[1] <= int(x, 16) < r[1] + r[2]:
                out(f'MEM [{x}] loaded from NAND {r[0] + int(x, 16) - r[1]:#x} lr={lr:#x}')
        uc.mem_write(r[1], nandRead(r[0], r[2])); ret(uc, 0); return
    if addr == NAND_WRITE:
        out(f'NAND write {r[0]:#x} len {r[2]}  lr={lr:#x}'); ret(uc, 0); return
    if addr == NAND_ERASE:
        out(f'NAND erase {r[0]:#x}  lr={lr:#x}'); ret(uc, 0); return
    if addr == NAND_TXBYTES:
        nandCmd[0] = uc.mem_read(r[0], 1)[0] if r[1] else 0
        ret(uc, 0); return
    if addr == NAND_RXBYTES:
        data = NAND_ID if nandCmd[0] == 0x9F else b''
        uc.mem_write(r[0], (data + b'\xff' * r[1])[:r[1]])
        ret(uc, 0); return
    if addr == WX_WRITE:
        chip, reg, val = r[2] & 0xFF, r[0] & 0xFF, r[1] & 0xFFFF
        wxRegs[(chip, reg)] = val
        if not a.quiet_wx or pttOn[0]:
            out(f'WX{chip} {reg:02X}={val:04X}  lr={lr:#x}')
        ret(uc, 0); return
    if addr == WX_READ:
        ret(uc, wxRegs[(r[1] & 0xFF, r[0] & 0xFF)]); return
    if addr == SCT_SEND:
        buf = bytes(uc.mem_read(r[0], r[1]))
        if buf[:3] != b'\x84\xa9\x61':      # raw interface word
            out(f'SCT> raw {buf.hex(" ")}  lr={lr:#x}')
            sctReply(0x00, b'\x17\x01')      # as the real chip answered
        return                                 # bytes still go through SCT_WR
    if addr == SCT_WR:
        sctHostByte(r[0] & 0xFF); ret(uc, 0); return
    if addr == SCT_RD:
        ret(uc, toHost.pop(0) if toHost else 0); return
    if addr == GPIO_READ:
        port, mask = r[0], r[1] & 0xFFFF
        gpioReads[(port, mask, lr)] += 1
        gpioLast[(port, mask, lr)] = tick[0]
        if port == GPIOD and mask & 0x880:
            ret(uc, 1 if toHost else 0)
        elif port == GPIOA and mask == 0x400:    # PTT, active low
            ret(uc, 0 if pttOn[0] else 1)
        elif port == GPIOB and mask == 0x40:     # bit-banged I2C SDA: ACK, data 0
            ret(uc, 0)
        else:
            idr = struct.unpack('<I', uc.mem_read(port + 0x10, 4))[0]
            ret(uc, 1 if idr & mask else 0)
        return
    if addr == GPIO_WRITE:
        port, mask, lvl = r[0], r[1] & 0xFFFF, r[2] & 1
        if port == GPIOD and mask & 0x100 and lvl:           # SCT3288 reset released
            sctReply(0x00, b'\x17\x0e')
        pins = [i for i in range(16) if mask >> i & 1]
        for i in pins:
            pinState[(port, i)] = lvl
        if any(p in LOGPINS.get(port, ()) for p in pins):
            out(f'GPIO {PORT.get(port, hex(port))}{pins}={lvl}  t={tick[0]}ms lr={lr:#x}')
        # keep IDR consistent with outputs written through the HAL
        idr = struct.unpack('<I', uc.mem_read(port + 0x10, 4))[0]
        idr = (idr | mask) if lvl else (idr & ~mask)
        uc.mem_write(port + 0x10, struct.pack('<I', idr))
        ret(uc, 0); return
    if addr in DELAYS:
        # 0x0811EC64 sleeps through OSTimeDly once uC/OS runs: keep that
        if addr == 0x0811EC64 and uc.mem_read(0x2000A479, 1)[0] == 1:
            return
        ret(uc, 0); return
    if addr == ERROR_HANDLER:
        uniq = list(dict.fromkeys(hex(x) for x in reversed(lastPeriphReads)))
        out(f'ERROR_HANDLER called from {lr:#x}, recent peripheral registers {uniq[:16]}')
        uc.emu_stop(); stopNow[0] = True; return

for x in (NAND_READ, NAND_WRITE, NAND_ERASE, NAND_TXBYTES, NAND_RXBYTES, WX_WRITE, WX_READ, SCT_SEND, SCT_WR, SCT_RD,
          GPIO_READ, GPIO_WRITE, ERROR_HANDLER) + DELAYS + (RET,):
    mu.hook_add(UC_HOOK_CODE, hook, begin=x, end=x)

def watchHook(uc, addr, size, _):
    out(f'WATCH {addr:#x} r0={uc.reg_read(UC_ARM_REG_R0):#x} r1={uc.reg_read(UC_ARM_REG_R1):#x} r2={uc.reg_read(UC_ARM_REG_R2):#x} r3={uc.reg_read(UC_ARM_REG_R3):#x} lr={uc.reg_read(UC_ARM_REG_LR):#x} t={tick[0]}ms')
for x in filter(None, a.watch.split(',')):
    mu.hook_add(UC_HOOK_CODE, watchHook, begin=int(x, 16), end=int(x, 16))
def memWatch(uc, access, addr, size, value, _):
    out(f'MEM [{addr:#x}]={value:#x} size={size} pc={uc.reg_read(UC_ARM_REG_PC):#x} '
        f'lr={uc.reg_read(UC_ARM_REG_LR):#x} t={tick[0]}ms')
for x in filter(None, a.watchmem.split(',')):
    mu.hook_add(UC_HOOK_MEM_WRITE, memWatch, begin=int(x, 16), end=int(x, 16))
readers = collections.Counter()
def memReadWatch(uc, access, addr, size, value, _):
    readers[(addr, uc.reg_read(UC_ARM_REG_PC), uc.reg_read(UC_ARM_REG_LR))] += 1
for x in filter(None, a.watchread.split(',')):
    mu.hook_add(UC_HOOK_MEM_READ, memReadWatch, begin=int(x, 16), end=int(x, 16) + 1)
callCount = collections.Counter()
def countHook(uc, addr, size, _):
    callCount[addr] += 1
for x in filter(None, a.count.split(',')):
    mu.hook_add(UC_HOOK_CODE, countHook, begin=int(x, 16), end=int(x, 16))

# --- peripheral status model -------------------------------------------------
lastPeriphReads = collections.deque(maxlen=400)
def periphRead(uc, access, addr, size, value, _):
    lastPeriphReads.append(addr)
    if (addr & ~0x1C00) == 0x42020010:
        idrReads[(addr, uc.reg_read(UC_ARM_REG_PC))] += 1
    v = struct.unpack('<I', uc.mem_read(addr & ~3, 4))[0]
    reg = addr & ~3
    if reg == 0x46020C00:                       # RCC_CR: ready follows on, both ways
        for onb, rdy in ((0, 2), (4, 5), (8, 10), (12, 13), (14, 15), (16, 17), (24, 25), (26, 27), (28, 29)):
            v = (v | (1 << rdy)) if (v >> onb & 1) else (v & ~(1 << rdy))
    elif reg == 0x46020CF0:                     # RCC_BDCR: LSE, LSE sys, LSI ready
        for onb, rdy in ((0, 1), (7, 11), (26, 27)):
            v = (v | (1 << rdy)) if (v >> onb & 1) else (v & ~(1 << rdy))
    elif reg == 0x4600780C:                     # RTC_ICSR: INITF follows INIT,
        v |= (1 << 5) | (1 << 2)                # RSF and WUTWF set, no shift
        v = (v | (1 << 6)) if (v >> 7 & 1) else (v & ~(1 << 6))
        v &= ~(1 << 3)
    elif reg == 0x46020C1C:                     # RCC_CFGR1: SWS = SW
        v = (v & ~0xC) | ((v & 3) << 2)
    elif reg == 0x4602083C:                     # PWR_SVMSR: everything ready
        v |= 0x0003F000 | 0x8000
    elif reg == 0x4602080C:                     # PWR_VOSR: booster and range ready
        v |= 0xC000
    elif 0x42028000 <= reg < 0x42028400 and (reg & 0xFF) == 0x00:    # ADC ISR
        v |= 0x0F
    elif 0x46021000 <= reg < 0x46021400 and (reg & 0xFF) == 0x00:    # ADC4 ISR
        v |= 0x0F
    elif (0x40020050 <= reg < 0x40020850) or (0x46025050 <= reg < 0x46025250):
        base = 0x40020050 if reg < 0x46000000 else 0x46025050
        off = (reg - base) % 0x80
        if off == 0x14:                         # GPDMA/LPDMA CxCR: a reset also
            if v & 0x2:                         # disables the channel
                v &= ~0x3
        elif off == 0x10:                       # CxSR: idle when not enabled
            cr = struct.unpack('<I', uc.mem_read(reg + 4, 4))[0]
            v = (v | 1) if not (cr & 1) else (v & ~1)
    elif reg in (0x42028008, 0x46021008):       # ADC CR: calibration, disable, stop done
        v &= ~((1 << 31) | (1 << 1) | (1 << 4) | (1 << 5))
        if (v >> 1) & 1:
            v &= ~1
    elif reg in (0x40003814, 0x40013014, 0x40003C14):                 # SPI SR
        v |= 0x1003
    elif reg in (0x4001381C, 0x4000441C, 0x4000481C, 0x40004C1C, 0x4000501C, 0x4600241C):
        v |= (1 << 7) | (1 << 6) | (1 << 21) | (1 << 22)   # USART ISR: TXE TC TEACK REACK
    elif reg == 0x42040010:                     # OTG GRSTCTL: AHB idle, resets
        v = (v | (1 << 31)) & ~0x31             # and FIFO flushes complete
    elif reg == 0x40022020:                     # FLASH NSSR: never busy
        v &= ~0x30000
    elif reg in (GPIOA + 0x10,):                # PA0 power switch on (low)
        v &= ~1
    uc.mem_write(reg, struct.pack('<I', v))
mu.hook_add(UC_HOOK_MEM_READ, periphRead, begin=0x40000000, end=0x5FFFFFFF)
def bkpWrite(uc, access, addr, size, value, _):
    out(f'BKP write [{addr:#x}]={value:#x}  pc={uc.reg_read(UC_ARM_REG_PC):#x}')
mu.hook_add(UC_HOOK_MEM_WRITE, bkpWrite, begin=0x46007D00, end=0x46007D7F)

# Inputs: keys and PTT released (high), power switch on (PA0 low)
for port in (GPIOA, GPIOB, GPIOC, GPIOD, GPIOE):
    idle = 0xFFFF
    if port == GPIOD:
        idle &= ~0x880                      # SCT3288 frame and ready lines low
    if port == GPIOA:
        idle &= ~0x1                        # PA0 low: power switch on (0x080D6954)
    mu.mem_write(port + 0x10, struct.pack('<I', idle))

# --- run ---------------------------------------------------------------------
def call(entry, args=(), count=int(5e7)):
    mu.reg_write(UC_ARM_REG_LR, RET | 1)
    for reg, v in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
        mu.reg_write(reg, v)
    mu.emu_start(entry | 1, RET, count=count)

mu.reg_write(UC_ARM_REG_SP, 0x20025FA0)
call(SCATTER)
out('---- .data/.bss initialised')
def peek(tag):
    for x in filter(None, a.peek.split(',')):
        out(f'PEEK {tag} [{x}]={mu.mem_read(int(x, 16), 1)[0]:#04x}')
peek('init')
for spec in filter(None, a.poke.split(',')):
    addr, data = spec.split(':'); mu.mem_write(int(addr, 16), bytes.fromhex(data))

# --- uC/OS context switches ---------------------------------------------------
# The stock port (PendSV handler 0x0800C2C8) is replaced by a switch done here:
# registers of a task that is switched out are kept in a dict, a task that
# never ran is started from the frame OSTaskStkInit built on its stack
# (r4-r11, s16-s31 if the FP frame is used, then the exception frame).
OS_CRIT_EXIT, OS_STARTHIGHRDY_LOOP = 0x0800C280, 0x0800C2A8
OS_CTXSW, OS_INTCTXSW = 0x0800C2AA, 0x0800C2B8
OS_TCBCUR, OS_TCBHIGHRDY = 0x2000A47C, 0x2000A484
OS_PRIOCUR, OS_PRIOHIGHRDY = 0x2000A3F2, 0x2000A3F3
OS_SWHOOK = 0x080C0844
ISR_STACK = 0x20025F00                  # main stack, unused once tasks run
REGS = ([UC_ARM_REG_R0 + i for i in range(13)] +
        [UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_XPSR, UC_ARM_REG_FPSCR] +
        [UC_ARM_REG_D0 + i for i in range(16)])
osState = {'pend': False, 'running': False, 'saved': {}, 'switches': 0, 'inIsr': False}

def rd32(addr):
    return struct.unpack('<I', mu.mem_read(addr, 4))[0]

def osSwitch(uc, resumePc, save=True):
    """Switch from OSTCBCur to OSTCBHighRdy, the current task resuming at resumePc."""
    osState['pend'] = False
    if save:
        ctx = [uc.reg_read(x) for x in REGS]
        osState['saved'][rd32(OS_TCBCUR)] = (ctx, resumePc)
    # PendSV bookkeeping (OSTaskSwHook, 0x080C0844, is empty)
    uc.mem_write(OS_PRIOCUR, bytes(uc.mem_read(OS_PRIOHIGHRDY, 1)))
    tcb = rd32(OS_TCBHIGHRDY)
    uc.mem_write(OS_TCBCUR, struct.pack('<I', tcb))
    osState['switches'] += 1
    if tcb in osState['saved']:
        ctx, pc = osState['saved'].pop(tcb)
        for x, v in zip(REGS, ctx):
            uc.reg_write(x, v)
    else:
        stk = rd32(tcb)
        words = struct.unpack('<64I', uc.mem_read(stk, 256))
        k = next(i for i in (15, 31) if words[i] & 0x01000000)     # xPSR, T bit
        ext = k == 31
        for i in range(8):
            uc.reg_write(UC_ARM_REG_R4 + i, words[i])
        for i in range(4):
            uc.reg_write(UC_ARM_REG_R0 + i, words[k - 7 + i])
        uc.reg_write(UC_ARM_REG_R12, words[k - 3])
        uc.reg_write(UC_ARM_REG_LR, words[k - 2])
        pc = words[k - 1]
        uc.reg_write(UC_ARM_REG_SP, stk + (k + 1 + (18 if ext else 0)) * 4)
        out(f'OS start task tcb={tcb:#x} prio={uc.mem_read(OS_PRIOCUR, 1)[0]} pc={pc:#x}')
    uc.reg_write(UC_ARM_REG_BASEPRI, 0)
    uc.reg_write(UC_ARM_REG_PRIMASK, 0)
    uc.reg_write(UC_ARM_REG_PC, pc | 1)

def osHook(uc, addr, size, _):
    lr = uc.reg_read(UC_ARM_REG_LR)
    if addr in (OS_CTXSW, OS_INTCTXSW):
        osState['pend'] = True
        uc.reg_write(UC_ARM_REG_PC, lr)
    elif addr == OS_CRIT_EXIT:
        # (in an ISR, OSIntExit pends the switch: it happens once the ISR returns)
        if (osState['pend'] and osState['running'] and not osState['inIsr']
                and uc.reg_read(UC_ARM_REG_R0) == 0):
            uc.reg_write(UC_ARM_REG_BASEPRI, 0)
            osSwitch(uc, lr)
    elif addr == OS_STARTHIGHRDY_LOOP:
        out('---- uC/OS scheduler started')
        osState['running'] = True
        osSwitch(uc, 0, save=False)
for x in (OS_CRIT_EXIT, OS_STARTHIGHRDY_LOOP, OS_CTXSW, OS_INTCTXSW):
    mu.hook_add(UC_HOOK_CODE, osHook, begin=x, end=x)

def isr(handler):
    """Run an interrupt handler on the main stack (or below the current
    stack before the scheduler runs)."""
    sp = ISR_STACK if osState['running'] else (mu.reg_read(UC_ARM_REG_SP) - 0x200) & ~7
    mu.reg_write(UC_ARM_REG_SP, sp)
    osState['inIsr'] = True
    try:
        call(handler, count=int(2e6))
    except UcError as e:
        out(f'ERR in handler {handler:#x}: {e}')
    osState['inIsr'] = False

# main, then the tasks, run in slices with the interrupts in between
mu.reg_write(UC_ARM_REG_SP, 0x20025FA0); mu.reg_write(UC_ARM_REG_LR, RET | 1)
pc = MAIN | 1
pcHist = collections.Counter(); t0 = time.time()
TIMCLK_KHZ = 160000
TIMERS = ((0x40000C00, 0x08120E70), (0x40001000, 0x08120EC0), (0x40001400, 0x08120EF8))  # TIM5-7
timAcc = {}
masked = 0
while tick[0] < a.ms:
    try:
        mu.emu_start(pc, RET, count=a.slice)
    except UcError as e:
        out(f'ERR {e} pc={mu.reg_read(UC_ARM_REG_PC):#x} lr={mu.reg_read(UC_ARM_REG_LR):#x}')
        break
    pc = mu.reg_read(UC_ARM_REG_PC) | 1
    if stopNow[0]:
        break
    if (pc & ~1) == RET:
        out('---- main returned'); break
    # Interrupts are taken only when not masked (critical sections)
    if osState['running'] and (mu.reg_read(UC_ARM_REG_BASEPRI) or mu.reg_read(UC_ARM_REG_PRIMASK) & 1):
        masked += 1
        if masked < 50:
            continue
        out(f'WARN interrupts masked for 50 slices at pc={pc:#x}')
    masked = 0
    ctx = [mu.reg_read(x) for x in REGS]
    isr(SYSTICK)
    # Basic and general-purpose timers with the update interrupt enabled
    for base, handler in TIMERS:
        cr1, dier = rd32(base), rd32(base + 0x0C)
        if not (cr1 & 1 and dier & 1):
            continue
        period = (rd32(base + 0x28) + 1) * (rd32(base + 0x2C) + 1) / TIMCLK_KHZ   # ms
        timAcc[base] = timAcc.get(base, 0) + 1
        n = 0
        while timAcc[base] >= period and n < 20:
            timAcc[base] -= period; n += 1
            mu.mem_write(base + 0x10, struct.pack('<I', rd32(base + 0x10) | 1))
            isr(handler)
        if n == 20:
            timAcc[base] = 0
    for x, v in zip(REGS, ctx):
        mu.reg_write(x, v)
    if osState['pend'] and osState['running']:
        mu.reg_write(UC_ARM_REG_PC, pc)
        osSwitch(mu, pc)
        pc = mu.reg_read(UC_ARM_REG_PC) | 1
    tick[0] += 1
    mu.mem_write(0x20001D36, struct.pack('<H', a.batt))
    if a.mic:
        import math
        mu.mem_write(0x20001D3A, struct.pack('<H', int(8192 + 3000 * math.sin(2 * math.pi * tick[0] / 7.3))))
    if a.periphw:
        ps, pe = (int(x) for x in a.periphw.split(':'))
        if tick[0] == ps:
            QUIET = {0x40003820, 0x40003814, 0x40003818, 0x40003800}       # SPI2 data/status
            def pw(uc, access, addr, size, value, _):
                if addr in QUIET or (addr & 0xFFFFFF00) in (0x40000C00, 0x40001000, 0x40001400):
                    return
                if 0x42020000 <= addr < 0x42021400 and (addr & 0x3FF) in (0x18, 0x28):
                    return                                                   # GPIO BSRR/BRR
                out(f'PW [{addr:#x}]={value:#x} pc={uc.reg_read(UC_ARM_REG_PC):#x} t={tick[0]}ms')
            pwH = mu.hook_add(UC_HOOK_MEM_WRITE, pw, begin=0x40000000, end=0x5FFFFFFF)
        elif tick[0] == pe:
            mu.hook_del(pwH)
    if a.trace:
        ts, te = (int(x) for x in a.trace.split(':'))
        if tick[0] == ts:
            FUNCSET = set(pickle.load(open(a.funcs, 'rb'))) if a.funcs != 'all' else None
            def traceBlk(uc, addr, size, _):
                if FUNCSET is None or addr in FUNCSET:
                    out(f'FN {addr:#x} lr={uc.reg_read(UC_ARM_REG_LR):#x} r0={uc.reg_read(UC_ARM_REG_R0):#x} '
                        f'tcb={rd32(OS_TCBCUR):#x}')
            traceH = mu.hook_add(UC_HOOK_BLOCK, traceBlk)
        elif tick[0] == te:
            mu.hook_del(traceH)
    if a.ptt:
        t0p, t1p = (int(x) for x in a.ptt.split(':'))
        on = t0p <= tick[0] < t1p
        if on != pttOn[0]:
            pttOn[0] = on
            idr = struct.unpack('<I', mu.mem_read(GPIOA + 0x10, 4))[0]
            idr = (idr & ~(1 << 10)) if on else (idr | (1 << 10))
            mu.mem_write(GPIOA + 0x10, struct.pack('<I', idr))
            out(f'==== PTT {"pressed" if on else "released"} t={tick[0]}ms')
    pcHist[pc & ~0x3F] += 1
    if tick[0] % 250 == 0:
        top = pcHist.most_common(1)[0]
        recent = list(dict.fromkeys(hex(x) for x in reversed(lastPeriphReads)))[:8]
        out(f'.... t={tick[0]}ms pc={pc:#x} hot={top[0]:#x}x{top[1]} '
            f'periph={recent} {time.time()-t0:.0f}s')
        sp = mu.reg_read(UC_ARM_REG_SP)
        words = struct.unpack('<128I', mu.mem_read(sp, 512))
        stack = [hex(w - 1) for w in words if (0x0800C001 <= w < 0x08200000) and (w & 1)][:12]
        out(f'     stack: {stack}')
        if top[1] >= 240:
            out(f'STUCK? around {top[0]:#x}, recent peripheral registers {recent}')
        pcHist.clear()
out(f'---- stopped at t={tick[0]}ms pc={pc:#x}')
for (port, mask, lr), n in sorted(gpioReads.items(), key=lambda x: -x[1])[:40]:
    out(f'read {PORT.get(port, hex(port))} mask={mask:#06x} lr={lr:#x} x{n} last t={gpioLast[(port, mask, lr)]}ms')
for (reg, pc), n in sorted(idrReads.items(), key=lambda x: -x[1])[:25]:
    out(f'IDR {reg:#x} pc={pc:#x} x{n}')
for x, n in sorted(callCount.items()):
    out(f'CALLS {x:#x} x{n}')
for tcb, (ctx, tpc) in sorted(osState['saved'].items()):
    sp = ctx[13]
    words = struct.unpack('<64I', mu.mem_read(sp, 256))
    frames = [hex(w - 1) for w in words if 0x0800C001 <= w < 0x08200000 and w & 1][:8]
    out(f'TASK tcb={tcb:#x} prio={mu.mem_read(tcb + 0x2E, 1)[0]} pc={tpc:#x} lr={ctx[14]:#x} stack={frames}')
for tcb in sorted(set(osState['saved']) | {rd32(OS_TCBCUR)}):
    out(f'TCB {tcb:#x}: {bytes(mu.mem_read(tcb, 0x54)).hex(" ")}')
out(f'OSIntNesting={mu.mem_read(0x2000A3F0,1)[0]} OSLockNesting={mu.mem_read(0x2000A3F1,1)[0]} '
    f'OSRdyGrp={mu.mem_read(0x2000A470,1)[0]:#04x} OSRdyTbl={bytes(mu.mem_read(0x2000A471, 8)).hex(" ")} '
    f'OSTCBCur={rd32(OS_TCBCUR):#x} switches={osState["switches"]}')
peek('end')
for port in (GPIOA, GPIOB, GPIOC, GPIOD, GPIOE):
    moder = rd32(port)
    st = ' '.join(f'{i}={pinState[(port, i)]}' for i in range(16) if (port, i) in pinState)
    out(f'PINS {PORT[port]} MODER={moder:08x} written: {st}')
for spec in filter(None, a.dump.split(',')):
    ad, ln = (int(x, 16) for x in spec.split(':'))
    for o in range(0, ln, 16):
        out(f'DUMP {ad + o:#010x}: ' + ' '.join(f'{w:08x}' for w in struct.unpack('<4I', bytes(mu.mem_read(ad + o, 16)))))
for (ad, pc, lr), n in sorted(readers.items(), key=lambda x: -x[1])[:20]:
    out(f'READ [{ad:#x}] pc={pc:#x} lr={lr:#x} x{n}')
out('WXREGS ' + ' '.join(f'{c}:{r:02X}={v:04X}' for (c, r), v in sorted(wxRegs.items())))
