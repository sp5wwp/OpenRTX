#!/bin/sh
#
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Host simulations of the M17 receiver, built with the system's compilers:
#   ifsim     the RT-950 Pro receive chain from its 8kHz IF (the real
#             at32_adc.c demodulator, then the M17 demodulator and decoder),
#             over carrier-to-noise ratios: ifsim 12 9 7 5 3; or a capture
#             from ifcap.py: ifsim replay capture.u16
#   syncsim   the M17 demodulator and decoder from a baseband with noise
#             (white, PARABOLIC=1, CLICKS=<per s>, OFFSET_HZ=<Hz>): syncsim
#             2000 3000; or false locks on noise alone: syncsim noise 1000
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../../.." && pwd)
OUT=${OUT:-$HERE}
cd "$ROOT"
INC="-I openrtx/include -I platform/drivers -I platform/targets/linux -I tests/platform"
M17=$(ls openrtx/src/protocols/M17/*.cpp | grep -v Modulator)
COMMON="$HERE/audio_stubs.cpp $M17 openrtx/src/core/dsp.cpp openrtx/src/core/crc.c tests/platform/M17Signal.cpp tests/platform/NoisyChannel.cpp"
gcc -std=gnu17 -O2 -c -I "$HERE/stubs" -I platform/drivers/audio -I openrtx/include \
    -I platform/targets/rt950pro -I platform/drivers "$HERE/ifdemod_host.c" -o "$OUT/ifdemod_host.o"
g++ -std=c++14 -O2 $INC "$HERE/ifsim.cpp" "$OUT/ifdemod_host.o" $COMMON -o "$OUT/ifsim"
g++ -std=c++14 -O2 $INC "$HERE/syncsim.cpp" $COMMON -o "$OUT/syncsim"
echo "built $OUT/ifsim and $OUT/syncsim"
