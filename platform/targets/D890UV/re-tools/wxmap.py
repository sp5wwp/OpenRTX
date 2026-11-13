#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# AnyTone AT-D890UV stock firmware reverse-engineering helper, see ../NOTES.md.
# wxmap.py <fw> <funcs.pkl>: map WX4818N register writes/reads to functions
import sys,pickle,bisect,collections
from capstone import *
B=0x0800C000;d=open(sys.argv[1],'rb').read();F=pickle.load(open(sys.argv[2],'rb'))
WR,RD=0x80e2f64,0x80e2cac
fn=lambda a:F[bisect.bisect_right(F,a)-1]
md=Cs(CS_ARCH_ARM,CS_MODE_THUMB)
res=collections.defaultdict(lambda: collections.defaultdict(set))
off=0x238; win=[]
while off<len(d):
    got=False
    for i in md.disasm(d[off:off+0x4000],B+off):
        got=True; win.append(i); win=win[-14:]
        if i.mnemonic=='bl' and int(i.op_str[1:],16) in (WR,RD):
            regs={}
            for j in win[:-1]:
                o=j.op_str.split(', ')
                try:
                    if j.mnemonic in('movs','movw','mov.w') and o[1].startswith('#'): regs[o[0]]=int(o[1][1:],16)
                    elif j.mnemonic=='movt' and o[0] in regs: regs[o[0]]|=int(o[1][1:],16)<<16
                    elif j.mnemonic in('movs','movw','mov.w','mov','ldr','ldrb','ldrh','ldr.w','ldrb.w','ldrh.w','adds','orr','orr.w','orrs','add','add.w'): regs.pop(o[0],None)
                except: regs.pop(o[0],None)
            kind='W' if int(i.op_str[1:],16)==WR else 'R'
            reg=regs.get('r0'); 
            if reg is None: continue
            val=regs.get('r1') if kind=='W' else None
            chip=regs.get('r2') if kind=='W' else regs.get('r1')
            res[(kind,reg&0xff)][fn(i.address)].add((None if val is None else val&0xffff, chip))
        off=i.address+i.size-B
    if not got: off+=2
for (k,r) in sorted(res, key=lambda x:(x[1],x[0])):
    fs=res[(k,r)]
    s='; '.join(f'{f:#x}:'+','.join(sorted({('?' if v is None else f'{v:04X}')+('' if c is None else f'@{c}') for v,c in vals})) for f,vals in sorted(fs.items()))
    print(f'{k} {r:02X} [{len(fs)}] {s[:400]}')
