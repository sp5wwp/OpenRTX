#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# AnyTone AT-D890UV stock firmware reverse-engineering helper, see ../NOTES.md.
# List the functions reading/writing fields of RAM structs:
# fieldrefs.py <firmware.CDD> <funcs.pkl> <hex base> [<hex base> ...]

# Find accesses to fields of RAM structs: movw/movt base into rX, then ldr/str [rX, #off]
import sys,pickle,bisect
from capstone import *
from capstone.arm import *
from collections import defaultdict
B=0x0800C000; d=open(sys.argv[1],'rb').read()
t=pickle.load(open(sys.argv[2],'rb'))
bases={int(x,16):x for x in sys.argv[3:]}
md=Cs(CS_ARCH_ARM,CS_MODE_THUMB); md.detail=True
res=defaultdict(set)
off=0x238; last={}; regbase={}
while off<len(d):
    got=False
    for ins in md.disasm(d[off:off+0x4000],B+off):
        got=True; m=ins.mnemonic
        if m=='movw' and ins.operands[1].type==ARM_OP_IMM: last[ins.operands[0].reg]=ins.operands[1].imm&0xffff
        elif m=='movt' and ins.operands[0].reg in last:
            v=(ins.operands[1].imm<<16)|last.pop(ins.operands[0].reg)
            if v in bases: regbase[ins.operands[0].reg]=(v,ins.address)
            else: regbase.pop(ins.operands[0].reg,None)
        elif m.startswith(('ldr','str')) and len(ins.operands)>=2 and ins.operands[-1].type==ARM_OP_MEM:
            mem=ins.operands[-1].mem
            if mem.base in regbase and ins.address-regbase[mem.base][1]<40:
                v=regbase[mem.base][0]
                f=t[bisect.bisect_right(t,ins.address)-1]
                res[(bases[v],mem.disp,m[:3])].add(hex(f))
        if m in('bl','blx','bx','pop'): regbase={}
        off=ins.address+ins.size-B
    if not got: off+=2
for k in sorted(res): print(k[0],hex(k[1]),k[2],len(res[k]),sorted(res[k])[:6])
