#!/bin/bash
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs inside the container. /work is the west workspace: it holds the
# OpenRTX checkout, and west puts the Zephyr fork and modules next to it.
# Extra arguments go to CMake, e.g. -DC62_BASEBAND_DUMP_BAUD=2000000.
set -euo pipefail
cd /work
# The mount belongs to the host user: trust it without writing a gitconfig
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0='*'
# Keep the compiler cache in the workspace, across container runs
export CCACHE_DIR=/work/.ccache

if [ ! -d OpenRTX ]; then
    echo "No OpenRTX checkout in /work: mount the folder that contains it." >&2
    exit 1
fi

# One-time workspace setup; "west update" is quick when nothing changed
[ -d .west ] || west init -l OpenRTX
west update --narrow --group-filter +csk6
export ZEPHYR_BASE=/work/zephyr-listenai

# Codec2 is a Meson subproject: fetch and patch it without configuring Meson
(cd OpenRTX && meson subprojects download codec2 >/dev/null)

cd OpenRTX
west build -b c62 -d build_c62 . -- "$@"

echo
echo "Done. Flash with cskburn (radio in burn mode):"
echo "  OpenRTX/build_c62/zephyr/zephyr.bin       at 0x000000"
echo "  OpenRTX/build_c62/zephyr/dsp_firmware.bin at 0x100000"
