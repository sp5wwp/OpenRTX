#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# AnyTone AT-D890UV stock firmware reverse-engineering helper, see ../NOTES.md.
# xref.py <fw> <funcs.pkl> <lo hex> [<hi hex>]: find code loading constants in [lo,hi]
import sys,pickle,bisect,struct
from capstone import *
from capstone.arm import *
B=0x0800C000; d=open(sys.argv[1],'rb').read(); F=pickle.load(open(sys.argv[2],'rb'))
lo=int(sys.argv[3],16); hi=int(sys.argv[4],16) if len(sys.argv)>4 else lo
md=Cs(CS_ARCH_ARM,CS_MODE_THUMB); md.detail=True
off=0x238; movw={}
while off<len(d):
    got=False
    for ins in md.disasm(d[off:off+0x4000],B+off):
        got=True; v=None
        if ins.id==ARM_INS_LDR and len(ins.operands)==2 and ins.operands[1].type==ARM_OP_MEM and ins.operands[1].mem.base==ARM_REG_PC:
            a=((ins.address+4)&~3)+ins.operands[1].mem.disp
            if B<=a<B+len(d)-4: v=struct.unpack('<I',d[a-B:a-B+4])[0]
        elif ins.mnemonic=='movw': movw[ins.operands[0].reg]=ins.operands[1].imm
        elif ins.mnemonic=='movt' and ins.operands[0].reg in movw: v=movw.pop(ins.operands[0].reg)|(ins.operands[1].imm<<16)
        if v is not None and lo<=v<=hi:
            i=bisect.bisect_right(F,ins.address)-1
            print(f'{ins.address:#x} in {F[i]:#x}: {v:#x}')
        off=ins.address+ins.size-B
    if not got: off+=2
