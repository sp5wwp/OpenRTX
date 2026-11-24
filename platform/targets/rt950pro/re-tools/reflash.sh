#!/bin/sh
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Flash an RT-950 Pro without pressing keys, wait for OpenRTX again.
#   reflash.sh <image.BTF> [port]
# Works from the stock firmware and from OpenRTX: both answer the
# PROGRAMBT9000U / UPDATE handshake of rt950_flash.py by restarting into the
# bootloader, which then accepts the update within its start-up window.
set -e
BTF=${1:?usage: reflash.sh <image.BTF> [port]}
PORT=${2:-/dev/ttyUSB0}
DIR=$(dirname "$0")
ROOT=$DIR/../../../..

# From OpenRTX the first attempt often only restarts the radio into the
# bootloader; the next one then uploads within its start-up window
n=0
until python3 "$ROOT/scripts/rt950_flash.py" upload "$PORT" "$BTF" 2>&1 \
        | tr '\r' '\n' | grep -q "upload complete"; do
    n=$((n + 1))
    if [ $n -ge 3 ]; then
        echo "upload failed" >&2
        exit 1
    fi
done
echo "Firmware upload complete"

# OpenRTX answers the monitor once it has started; the stock firmware does not
i=0
while [ $i -lt 20 ]; do
    if python3 "$DIR/monitor.py" --port "$PORT" ping 2>/dev/null; then
        # The RTC runs from the main oscillator, stopped while flashing
        python3 "$DIR/rtcsync.py" --port "$PORT"
        exit 0
    fi
    sleep 0.5; i=$((i + 1))
done
echo "no answer from the OpenRTX monitor (stock firmware flashed?)" >&2
exit 1
