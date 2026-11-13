#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# AnyTone AT-D890UV stock firmware reverse-engineering helper, see ../NOTES.md.
# Emulate a stock routine and log WX4818N register accesses, GPIO writes and
# pin configurations: FREQ=<10Hz units> trace.py <firmware.CDD> <hex entry> [args]
# Options (environment):
#   RAM=<hexaddr>:<file>[@<hexoffset>:<hexlen>],...  load RAM images first,
#       e.g. the settings block dumped from the radio at 0x200148E4
#   PERIPH=1  also log direct peripheral writes (DAC, timers, ...), not GPIO
#   MAXI=<n>  instruction limit (default 2e7)
#   SCATTER=1  initialise .data/.bss with the stock start-up code first
#   POKE=<hexaddr>:<hexbytes>,...  write bytes after loading RAM images
#   STOP=<hexaddr>  stop when execution reaches this address, to emulate
#       a block inside a function: the entry is then any instruction, run
#       with a fresh 0x100-byte stack frame (FRAME=<hexbytes> to preset it)
#   DUMP=<hexaddr>:<hexlen>,...  print memory after the run
#   RTRACE=<hexlo>:<hexhi>  log data reads in that address range
#   STUBS=<hexaddr>[:<ret>],...  skip these functions, log r0-r3 and return
#       <ret> (default 0), e.g. the NAND read 0x08037E78
#   THEN=<hexaddr>[:<arg>:<arg>...],...  then run these functions in turn in
#       the same memory, e.g. a command queue followed by its sender
#   STUBBUF=<hexaddr>,...  like STUBS, for functions taking (buffer, length):
#       also log the buffer bytes, e.g. the SCT3288 packet send 0x0802E8F8

# Emulate a stock routine; log WX4818N register access, GPIO writes and delays
import sys,struct
from unicorn import *
from unicorn.arm_const import *
B=0x0800C000
fw=open(sys.argv[1],'rb').read(); entry=int(sys.argv[2],16)
args=[int(x,0) for x in sys.argv[3:]]
WR,RD,PIN,INIT=0x80e2f64,0x80e2cac,0x804fbd8,0x804f7d0
PORT={0x42020000+i*0x400:'P'+'ABCDE'[i] for i in range(5)}
mu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
mu.mem_map(0x0,0x10000); mu.mem_map(0x08000000,0x200000); mu.mem_write(B,fw)
mu.mem_map(0x20000000,0x100000); mu.mem_map(0x40000000,0x20000000); mu.mem_map(0xE0000000,0x100000)
import os
FREQ=int(os.environ.get('FREQ','43500000'))
for a in (0x2000432c,0x2000c240,0x2000c244,0x2001d780,0x2001d784): mu.mem_write(a,struct.pack('<I',FREQ))
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
    if '@' in rest:
        fn,rng=rest.split('@'); o,n=(int(x,16) for x in rng.split(':'))
        blob=open(fn,'rb').read()[o:o+n]
    else:
        blob=open(rest,'rb').read()
    mu.mem_write(int(addr,16),blob)
for spec in filter(None,os.environ.get('POKE','').split(',')):
    addr,data=spec.split(':'); mu.mem_write(int(addr,16),bytes.fromhex(data))
RET=0x20090000; mu.mem_write(RET,b'\x00\xbf\xfe\xe7')
STOP=int(os.environ['STOP'],16) if os.environ.get('STOP') else None
log=[]
STUBS={}
for spec in filter(None,os.environ.get('STUBS','').split(',')):
    a,_,r=spec.partition(':'); STUBS[int(a,16)]=int(r,0) if r else 0
STUBBUF={int(a,16) for a in filter(None,os.environ.get('STUBBUF','').split(','))}
for a in STUBBUF: STUBS.setdefault(a,0)
def hook(uc,addr,size,_):
    if addr==RET or addr==STOP: uc.emu_stop(); return
    if addr in STUBS:
        a=[uc.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)]
        lr=uc.reg_read(UC_ARM_REG_LR)
        if addr in STUBBUF:
            data=bytes(uc.mem_read(a[0],min(a[1],512)))
            log.append(f'BUF  {addr:#x} len={a[1]}: {data.hex(" ")}  lr={lr:#x}')
        else:
            log.append(f'CALL {addr:#x}({", ".join(hex(x) for x in a)})  lr={lr:#x}')
        uc.reg_write(UC_ARM_REG_R0,STUBS[addr]); uc.reg_write(UC_ARM_REG_PC,lr); return
    if addr==INIT:
        r0,r1=(uc.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1))
        pin,mode,pull=struct.unpack('<III',uc.mem_read(r1,12))
        log.append(f'CFG  {PORT.get(r0,hex(r0))}{[i for i in range(16) if pin>>i&1]} mode={mode} pull={pull}')
        uc.reg_write(UC_ARM_REG_R0,0); uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR)); return
    if addr in (WR,RD,PIN):
        r0,r1,r2=(uc.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2))
        lr=uc.reg_read(UC_ARM_REG_LR)
        if addr==WR: log.append(f'WX{r2&0xff} W 0x{r0&0xff:02X}=0x{r1&0xffff:04X}  lr={lr:#x}')
        elif addr==RD: log.append(f'WX{r1&0xff} R 0x{r0&0xff:02X}')
        else: log.append(f'GPIO {PORT.get(r0,hex(r0))}{[i for i in range(16) if r1>>i&1]}={r2}  lr={lr:#x}')
        uc.reg_write(UC_ARM_REG_R0,0); uc.reg_write(UC_ARM_REG_PC,lr)
def pwrite(uc,access,addr,size,value,_):
    if 0x42020000<=addr<0x42021400: return   # GPIO ports, logged via the HAL calls
    log.append(f'MEM  [{addr:#010x}]={value:#x} ({size})  pc={uc.reg_read(UC_ARM_REG_PC):#x}')
def rread(uc,access,addr,size,value,_):
    v=int.from_bytes(uc.mem_read(addr,size),'little')
    log.append(f'RD   [{addr:#010x}]={v:#x} ({size})  pc={uc.reg_read(UC_ARM_REG_PC):#x}')
if os.environ.get('RTRACE'):
    lo,hi=(int(x,16) for x in os.environ['RTRACE'].split(':'))
    mu.hook_add(UC_HOOK_MEM_READ,rread,begin=lo,end=hi)
if os.environ.get('PERIPH'):
    mu.hook_add(UC_HOOK_MEM_WRITE,pwrite,begin=0x40000000,end=0x5fffffff)
mu.hook_add(UC_HOOK_CODE,hook,begin=0x08000000,end=0x08200000)
mu.hook_add(UC_HOOK_CODE,hook,begin=RET,end=RET+4)
mu.reg_write(UC_ARM_REG_SP,0x20088000); mu.reg_write(UC_ARM_REG_LR,RET|1)
if STOP is not None:
    mu.reg_write(UC_ARM_REG_SP,0x20087F00)
    frame=bytes.fromhex(os.environ.get('FRAME',''))
    mu.mem_write(0x20087F00,frame+bytes(0x100-len(frame)))
for r,v in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2),args): mu.reg_write(r,v)
runs=[(entry,args)]
for spec in filter(None,os.environ.get('THEN','').split(',')):
    f,*a=spec.split(':'); runs.append((int(f,16),[int(x,0) for x in a]))
for n,(ent,arg) in enumerate(runs):
    if n:
        log.append(f'---- {ent:#x}({", ".join(hex(x) for x in arg)})')
        mu.reg_write(UC_ARM_REG_SP,0x20088000); mu.reg_write(UC_ARM_REG_LR,RET|1)
        for r,v in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),arg): mu.reg_write(r,v)
    try: mu.emu_start(ent|1,RET,count=int(float(os.environ.get('MAXI','2e7'))))
    except UcError as e: log.append(f'ERR {e} pc={mu.reg_read(UC_ARM_REG_PC):#x}')
for spec in filter(None,os.environ.get('DUMP','').split(',')):
    a,n=(int(x,16) for x in spec.split(':'))
    log.append(f'DUMP {a:#010x}: {bytes(mu.mem_read(a,n)).hex()}')
print('\n'.join(log))
