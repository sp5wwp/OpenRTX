#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# AnyTone AT-D890UV stock firmware reverse-engineering helper, see ../NOTES.md.
# sensitivity.py <entry> [args..]: which RAM bytes read by a stock routine change its
# WX4818N/GPIO/peripheral output. Uses trace.py, with its environment options;
# FW=<firmware.CDD> is required.
import sys,os,subprocess,re
TR=os.path.join(os.path.dirname(os.path.abspath(__file__)),'trace.py')
FW=os.environ['FW']; PY=sys.executable
def run(extra_poke='',rtrace=False):
    env=dict(os.environ)
    if extra_poke: env['POKE']=(env.get('POKE','')+','+extra_poke).strip(',')
    if rtrace: env['RTRACE']='20000000:2007ffff'
    out=subprocess.run([PY,TR,FW]+sys.argv[1:],capture_output=True,text=True,env=env).stdout
    return out
base=run(rtrace=True)
reads=sorted({int(m,16) for m in re.findall(r'RD +\[(0x[0-9a-f]+)\]',base)})
reads=[a for a in reads if not (0x20087000<=a<0x20089000)]
clean=lambda o:[re.sub(r'  (lr|pc)=.*','',l) for l in o.splitlines() if not l.startswith('RD')]
ref=clean(run())
print(f'{len(reads)} RAM bytes read; baseline {len(ref)} output lines')
for a in reads:
    for v in ('01','ff'):
        out=clean(run(f'{a:x}:{v}'))
        if out!=ref:
            diff=[l for l in out if l not in ref][:6]
            print(f'{a:#010x}={v}: '+'; '.join(diff)); break
