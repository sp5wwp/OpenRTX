#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Set the RT-950 Pro RTC to the UTC time of this computer, and the timezone
# setting to its UTC offset, through the debug monitor; then read it back.
#   rtcsync.py [--port /dev/ttyUSB0]
# The RTC runs from the main crystal: it stops while the radio is off, so the
# time is to be set again after each power-up (or taken from GPS).
import sys, time, datetime
sys.dont_write_bytecode = True
from monitor import Monitor

port = sys.argv[2] if sys.argv[1:2] == ['--port'] else '/dev/ttyUSB0'
m = Monitor(port)

# Send right after a second boundary, so the second sent is the current one
time.sleep(1.0 - (time.time() % 1.0) + 0.005)
now = datetime.datetime.now(datetime.timezone.utc)
m.cmd(f'rtc {now.year % 100} {now.month} {now.day} {now.hour} {now.minute} '
      f'{now.second} {now.isoweekday()}')

# Timezone setting: the UTC offset of this computer, in half hours
offset = datetime.datetime.now().astimezone().utcoffset()
m.cmd(f'tz {round(offset.total_seconds() / 1800)}')

time.sleep(1.0 - (time.time() % 1.0) + 0.3)
pc = datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0)
radio = m.cmd('rtc')[0]
rt = datetime.datetime.strptime(radio[:19], '%Y-%m-%d %H:%M:%S').replace(
    tzinfo=datetime.timezone.utc)
print(f'PC    {pc:%Y-%m-%d %H:%M:%S} UTC')
print(f'radio {radio}')
print(f'offset {(rt - pc).total_seconds():+.0f} s; timezone set to UTC{offset.total_seconds() / 3600:+g}')
