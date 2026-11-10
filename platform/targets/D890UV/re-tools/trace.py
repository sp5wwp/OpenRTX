#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# AnyTone AT-D890UV stock firmware reverse-engineering helper, see ../NOTES.md.
# Emulate a stock routine and log WX4818N register accesses, GPIO writes and
# pin configurations: FREQ=<10Hz units> trace.py <firmware.CDD> <hex entry> [args]

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
RET=0x20090000; mu.mem_write(RET,b'\x00\xbf\xfe\xe7')
log=[]
def hook(uc,addr,size,_):
    if addr==RET: uc.emu_stop(); return
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
mu.hook_add(UC_HOOK_CODE,hook,begin=0x08000000,end=0x08200000)
mu.hook_add(UC_HOOK_CODE,hook,begin=RET,end=RET+4)
mu.reg_write(UC_ARM_REG_SP,0x20088000); mu.reg_write(UC_ARM_REG_LR,RET|1)
for r,v in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2),args): mu.reg_write(r,v)
try: mu.emu_start(entry|1,RET,count=int(2e7))
except UcError as e: log.append(f'ERR {e} pc={mu.reg_read(UC_ARM_REG_PC):#x}')
print('\n'.join(log))
