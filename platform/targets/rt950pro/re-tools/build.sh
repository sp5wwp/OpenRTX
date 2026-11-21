#!/bin/sh
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build the RT-950 Pro firmware in the OpenRTX Zephyr container.
#   build.sh [image]        image defaults to openrtx-zephyr-rt950:local
#   PRISTINE=1 build.sh     rebuild from scratch: needed when a device tree
#                           change must reach a Kconfig default derived from
#                           it (e.g. the system clock frequency)
# The west workspace lives in the openrtx-workspace volume. CMake runs every
# time, because the target folder is copied to build/board_root only when
# CMake configures. The old .BTF is removed first, so that a failed build
# cannot be flashed by mistake.
set -e
IMAGE=${1:-openrtx-zephyr-rt950:local}
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd)
rm -f "$ROOT/build_rt950pro/openrtx_rt950pro.BTF"
docker run --rm -v openrtx-workspace:/workspaces/openrtx \
    -v "$ROOT":/workspaces/openrtx/OpenRTX -w /workspaces/openrtx/OpenRTX \
    -e ZEPHYR_BASE=/workspaces/openrtx/zephyr-artery \
    -e PRISTINE="${PRISTINE:-0}" "$IMAGE" \
    sh -c 'P=auto; [ "$PRISTINE" = 1 ] && P=always; \
           west build -p $P -b rt950pro -d build_rt950pro --cmake . \
               > build_rt950pro.log 2>&1 \
           || { grep -E "error|Error" build_rt950pro.log | head -20; exit 1; }'
grep -E "FLASH:|RAM:" "$ROOT/build_rt950pro.log"
