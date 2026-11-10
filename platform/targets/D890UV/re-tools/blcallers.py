#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# AnyTone AT-D890UV stock firmware reverse-engineering helper, see ../NOTES.md.
# List the call sites of a function: blcallers.py <firmware.CDD> <hex address>

import sys
from capstone import *
B=0x0800C000; d=open(sys.argv[1],'rb').read(); want=int(sys.argv[2],16)
md=Cs(CS_ARCH_ARM,CS_MODE_THUMB); off=0x238
while off<len(d):
    got=False
    for ins in md.disasm(d[off:off+0x4000],B+off):
        got=True
        if ins.mnemonic=='bl' and int(ins.op_str[1:],16)==want: print(hex(ins.address))
        off=ins.address+ins.size-B
    if not got: off+=2
