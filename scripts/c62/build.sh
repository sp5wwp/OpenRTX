#!/bin/bash
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs inside the container. /work is the west workspace: it holds the
# OpenRTX checkout, and west puts the Zephyr fork and modules next to it.
# Extra arguments go to CMake, e.g. -DC62_BASEBAND_DUMP_BAUD=2000000.
#
# TETRA speech needs the ETSI EN 300 395-2 codebooks. This script fetches
# them from etsi.org; if that fails, download the ZIP in a web browser, put
# it in the workspace folder (next to OpenRTX) and run again, or pass its
# path with -e ETSI_ZIP=/work/<path>.
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

# ETSI codebooks for TETRA speech (OpenACELP), generated once
OA=/work/modules/lib/openacelp
ETSI_FILE=en_30039502v010303p0.zip
ETSI_URL=https://www.etsi.org/deliver/etsi_en/300300_300399/30039502/01.03.03_60/$ETSI_FILE
if [ ! -f "$OA/build/tetra_tables.c" ]; then
    zip="${ETSI_ZIP:-}"
    if [ -z "$zip" ] && [ -f "/work/$ETSI_FILE" ]; then
        zip="/work/$ETSI_FILE"
    fi
    args=()
    if [ -n "$zip" ]; then
        if [ ! -f "$zip" ]; then
            echo "ETSI_ZIP=$zip: no such file (it must be inside the" \
                 "workspace folder, seen as /work in the container)" >&2
            exit 1
        fi
        args=(ETSI_ZIP="$(realpath "$zip")")
        echo "Generating the ETSI codebooks from $zip"
    else
        echo "Downloading ETSI EN 300 395-2 for the TETRA speech codebooks."
        echo "By continuing you accept ETSI's terms of use for this material."
    fi
    if ! out=$(make -C "$OA" tetra-tables "${args[@]}" 2>&1); then
        if grep -q "403" <<<"$out"; then
            reason="etsi.org refused the download (HTTP 403)."
        elif [ -n "$zip" ]; then
            reason="$zip is not the expected file, or could not be read:"
        else
            reason="the download from etsi.org failed:"
        fi
        cat >&2 <<MSG

******************************************************************************
WARNING: no TETRA speech in this build: $reason
$(grep -v "^make\|^Download the file\|^  http\|^then run\|^  make tetra\|^Automatic" <<<"$out" | tail -n 4 | sed 's/^/  /')

To add speech, download this file in a web browser:
  $ETSI_URL
put it in your workspace folder (next to OpenRTX) and build again, or pass
its path with: docker run ... -e ETSI_ZIP=/work/path/to/$ETSI_FILE ...
******************************************************************************

MSG
    fi
fi

cd OpenRTX
# --cmake: re-run CMake, which picks up newly generated ETSI codebooks
west build --cmake -b c62 -d build_c62 . -- "$@"

echo
if [ -f "$OA/build/tetra_tables.c" ]; then
    echo "TETRA speech is enabled: this firmware contains ETSI codebooks."
    echo "Do not distribute it."
fi
echo "Done. Flash with cskburn (radio in burn mode):"
echo "  OpenRTX/build_c62/zephyr/zephyr.bin       at 0x000000"
echo "  OpenRTX/build_c62/zephyr/dsp_firmware.bin at 0x100000"
