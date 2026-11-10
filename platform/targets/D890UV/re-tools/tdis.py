#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# AnyTone AT-D890UV stock firmware reverse-engineering helper, see ../NOTES.md.
# Disassemble: tdis.py <firmware.CDD> <hex address> <count>

import sys
from capstone import *
B=0x0800C000; d=open(sys.argv[1],'rb').read(); a=int(sys.argv[2],16); n=int(sys.argv[3])
md=Cs(CS_ARCH_ARM,CS_MODE_THUMB)
for i,ins in enumerate(md.disasm(d[a-B:a-B+n*4],a)):
    if i>=n: break
    print(f'{ins.address:#x} {ins.mnemonic} {ins.op_str}')
