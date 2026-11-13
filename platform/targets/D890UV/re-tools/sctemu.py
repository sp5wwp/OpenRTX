#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# AnyTone AT-D890UV stock firmware reverse-engineering helper, see ../NOTES.md.
# Emulate stock routines that talk to the SCT3288 baseband, with a fake chip
# on the SPI byte layer, and log the packets the stock library sends.
#   sctemu.py <firmware.CDD> <hexentry>[:arg:arg..][@hexstop] [...]
# With @hexstop the entry may be any instruction inside a function: the run
# stops at that address, with a fresh zeroed 0x100-byte stack frame.
# Environment: RAM, POKE, SCATTER as for trace.py; MAXI instruction limit per entry;
# CALLS=<n> also log the first n function entries (needs FUNCS=funcs.pkl).
# The fake chip answers each complete host packet by echoing it, so library
# waiters that look for a reply with the same field identifier go on. When
# its reset line (PD8) is released it sends the boot status the library
# waits for (0x0810061C): ACK_MESSAGE (field 0x17) = 0x0E. Raw words without
# the packet header (interface setup, 0x08100664) get ACK_MESSAGE = 0.
# CODEC_SELECT gets 2D 00, as the real chip answers (checked on the radio).
import sys,os,struct
from unicorn import *
from unicorn.arm_const import *
B=0x0800C000
fw=open(sys.argv[1],'rb').read()
WRBYTE,RDBYTE=0x0810387C,0x08103838   # SPI byte write / read hooks
SENDBUF=0x0810A674                     # (buffer, length) send, logged raw
GPIO_READ,GPIO_WRITE=0x0804FBA8,0x0804FBD8
DELAYS=(0x0811EC64,0x080F0600)
GPIOD=0x42020C00
PORT={0x42020000+i*0x400:'P'+'ABCDE'[i] for i in range(5)}
mu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
mu.mem_map(0x0,0x10000); mu.mem_map(0x08000000,0x200000); mu.mem_write(B,fw)
mu.mem_map(0x20000000,0x100000); mu.mem_map(0x40000000,0x20000000); mu.mem_map(0xE0000000,0x100000)
SCATTER_INIT=0x080101D0   # Keil __scatterload + library init, called by __main
def scatterLoad(uc):
    """Initialise .data/.bss as the stock start-up does (0x0800C238)."""
    ret=0x20090000; uc.mem_write(ret,b'\x00\xbf\xfe\xe7')
    uc.reg_write(UC_ARM_REG_SP,0x20025FA0); uc.reg_write(UC_ARM_REG_LR,ret|1)
    uc.emu_start(SCATTER_INIT|1,ret,count=int(5e7))
if os.environ.get('SCATTER'):
    scatterLoad(mu)
for spec in filter(None,os.environ.get('RAM','').split(',')):
    addr,rest=spec.split(':',1)
    fn,_,rng=rest.partition('@'); blob=open(fn,'rb').read()
    if rng: o,n=(int(x,16) for x in rng.split(':')); blob=blob[o:o+n]
    mu.mem_write(int(addr,16),blob)
for spec in filter(None,os.environ.get('POKE','').split(',')):
    addr,data=spec.split(':'); mu.mem_write(int(addr,16),bytes.fromhex(data))
RET=0x20090000; mu.mem_write(RET,b'\x00\xbf\xfe\xe7')
host=bytearray(); toHost=bytearray(); log=[]

def decode(p):
    """Describe one packet: header, type and fields (id + data)."""
    length=(p[3]<<8)|p[4]; t=p[5]
    flags=''.join(n for b,n in ((7,'V'),(6,'C'),(5,'R'),(4,'F')) if t>>b&1)
    body=p[6:6+length]
    return f'type={t&0xF}{("/"+flags) if flags else ""} len={length} body={body.hex(" ")}'

def hostByte(b):
    host.append(b)
    while len(host)>=3 and host[:3]!=b'\x84\xa9\x61': host.pop(0)
    if len(host)>=6:
        n=6+((host[3]<<8)|host[4])
        if len(host)>=n:
            p=bytes(host[:n]); del host[:n]
            if n%2 and host[:1]==b'\x00': host.pop(0)
            log.append(f'HOST {decode(p)}')
            body=p[6:]
            if p[5]==0x00 and body[:1]==b'\x2d':
                # CODEC_SELECT: the real chip answers 2D 00 (accepted)
                toHost.extend(b'\x84\xa9\x61\x00\x02\x00\x2d\x00')
            else:
                toHost.extend(p)        # echo as the reply

def hook(uc,addr,size,_):
    if addr==RET: uc.emu_stop(); return
    lr=uc.reg_read(UC_ARM_REG_LR)
    r=[uc.reg_read(x) for x in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2)]
    ret=None
    if addr==SENDBUF:
        buf=bytes(uc.mem_read(r[0],r[1]))
        log.append(f'SEND {buf.hex(" ")}  lr={lr:#x}')
        if buf[:3]!=b'\x84\xa9\x61':
            # Raw interface word (0x08100664): answer ACK_MESSAGE = 0
            toHost.extend(b'\x84\xa9\x61\x00\x02\x00\x17\x00')
    if addr==WRBYTE: hostByte(r[0]&0xFF); ret=0
    elif addr==RDBYTE: ret=toHost.pop(0) if toHost else 0
    elif addr==GPIO_READ:
        # PD11 = data ready, PD7 = frame (read mode after the open)
        ret=1 if (r[0]==GPIOD and r[1]&0x880 and toHost) else 0
    elif addr==GPIO_WRITE:
        if r[0]==GPIOD and r[1]&0x100 and r[2]:          # reset released
            toHost.extend(b'\x84\xa9\x61\x00\x02\x00\x17\x0e')
            log.append('CHIP boot status 17 0E')
        if not (r[0]==0x42020400 and r[1]==0x1000):      # skip chip select noise
            log.append(f'GPIO {PORT.get(r[0],hex(r[0]))}{[i for i in range(16) if r[1]>>i&1]}={r[2]}  lr={lr:#x}')
        ret=0
    elif addr in DELAYS: ret=0
    if ret is not None:
        uc.reg_write(UC_ARM_REG_R0,ret); uc.reg_write(UC_ARM_REG_PC,lr)
CALLS=int(os.environ.get('CALLS','0'))
if CALLS:
    import pickle
    FUNCS=set(pickle.load(open(os.environ['FUNCS'],'rb')))
    seen=[0]
    def calls(uc,addr,size,_):
        if addr in FUNCS and seen[0]<CALLS:
            seen[0]+=1; log.append(f'CALL {addr:#x}  lr={uc.reg_read(UC_ARM_REG_LR):#x}')
    mu.hook_add(UC_HOOK_CODE,calls,begin=0x08000000,end=0x08200000)
# Hook only the stubbed addresses: a hook on every instruction makes the
# emulation about a hundred times slower
for a in (WRBYTE,RDBYTE,GPIO_READ,GPIO_WRITE,SENDBUF)+DELAYS:
    mu.hook_add(UC_HOOK_CODE,hook,begin=a,end=a)
mu.hook_add(UC_HOOK_CODE,hook,begin=RET,end=RET+4)
for spec in sys.argv[2:]:
    spec,_,stop=spec.partition('@')
    f,*a=spec.split(':'); ent=int(f,16); args=[int(x,0) for x in a]
    log.append(f'==== {ent:#x}({", ".join(hex(x) for x in args)}){" to "+stop if stop else ""}')
    mu.reg_write(UC_ARM_REG_SP,0x20088000); mu.reg_write(UC_ARM_REG_LR,RET|1)
    if stop:
        mu.mem_write(0x20087F00,bytes(0x100)); mu.reg_write(UC_ARM_REG_SP,0x20087F00)
        stopAt=int(stop,16); mu.hook_add(UC_HOOK_CODE,lambda uc,a,s,_:uc.emu_stop(),begin=stopAt,end=stopAt)
    for reg,v in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args): mu.reg_write(reg,v)
    try: mu.emu_start(ent|1,RET,count=int(float(os.environ.get('MAXI','2e7'))))
    except UcError as e: log.append(f'ERR {e} pc={mu.reg_read(UC_ARM_REG_PC):#x}')
    log.append(f'---- returned r0={mu.reg_read(UC_ARM_REG_R0):#x}, pc={mu.reg_read(UC_ARM_REG_PC):#x}')
print('\n'.join(log))
