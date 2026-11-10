#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# AnyTone AT-D890UV stock firmware reverse-engineering helper, see ../NOTES.md.
# Build the function entry index: findex.py <firmware.CDD> <funcs.pkl>

# Build a sorted list of function entry points: bl targets plus push {.., lr} sites
import sys,pickle
from capstone import *
B=0x0800C000; d=open(sys.argv[1],'rb').read()
md=Cs(CS_ARCH_ARM,CS_MODE_THUMB)
t=set(); off=0x238
while off<len(d):
    got=False
    for ins in md.disasm(d[off:off+0x4000],B+off):
        got=True
        if ins.mnemonic=='bl': t.add(int(ins.op_str[1:],16))
        elif ins.mnemonic=='push' and 'lr' in ins.op_str: t.add(ins.address)
        off=ins.address+ins.size-B
    if not got: off+=2
pickle.dump(sorted(t),open(sys.argv[2],'wb')); print(len(t),'entries')
