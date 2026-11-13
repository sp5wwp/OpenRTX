#!/bin/sh
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Flash a running OpenRTX AT-D890UV without pressing keys: send the monitor
# "update" command, wait for the bootloader, flash, wait for OpenRTX again.
#   FLASHER=/path/to/flash_anytone.py reflash.sh <image.spi> [port]
# flash_anytone.py: https://github.com/sp5wwp/anytone-flasher
set -e
SPI=${1:?usage: FLASHER=... reflash.sh <image.spi> [port]}
PORT=${2:-/dev/ttyACM0}
: "${FLASHER:?set FLASHER to the path of flash_anytone.py}"
DIR=$(dirname "$0")

# Wait until a USB ID is present and the port is usable
waitfor() {
    i=0
    while [ $i -lt 100 ]; do
        if lsusb | grep -q "$1" && [ -w "$PORT" ]; then return 0; fi
        sleep 0.2; i=$((i + 1))
    done
    echo "timeout waiting for $1" >&2; return 1
}

if lsusb | grep -q 0483:4001; then
    waitfor 0483:4001
    python3 "$DIR/monitor.py" --port "$PORT" update
fi
waitfor 0483:5740
sleep 0.5
python3 "$FLASHER" "$SPI" "$PORT" --yes
waitfor 0483:4001
python3 "$DIR/monitor.py" --port "$PORT" ping
