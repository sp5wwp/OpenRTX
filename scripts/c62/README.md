<!--
 - SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 -
 - SPDX-License-Identifier: GPL-3.0-or-later
-->

# Retevis C62: build, flash, capture RX baseband

## 1. Build with Docker

You only need Docker and git. The image holds the toolchain (Zephyr SDK
0.16.8, `west`, CMake, Ninja). Your source tree stays on your disk and is
mounted into the container. ListenAI's `lisa` tool is not needed.

The C62 builds against ListenAI's Zephyr 3.4 fork. `west` puts it, and the
CSK6 modules, next to the OpenRTX checkout, so make a folder to hold both:

```bash
mkdir c62-ws && cd c62-ws
git clone -b c62/m17 https://github.com/turnrye/OpenRTX.git
# (if you were given patches: git -C OpenRTX am ../*.patch)

# Build the image once (5-10 minutes, ~2 GB)
docker build -t openrtx-c62 OpenRTX/scripts/c62

# Build the firmware. The first run downloads the Zephyr fork (~1.5 min).
docker run --rm -it --user "$(id -u):$(id -g)" \
    -v "$PWD":/work openrtx-c62
```

The outputs are:

| File | Flash at |
|---|---|
| `OpenRTX/build_c62/zephyr/zephyr.bin` | `0x000000` |
| `OpenRTX/build_c62/zephyr/dsp_firmware.bin` (downloaded, hash-checked) | `0x100000` |

Run the same `docker run` command after each code change. The build is
incremental.

**TETRA speech (ETSI codebooks).** Playing TETRA speech needs the codebooks
of the ETSI ACELP codec (EN 300 395-2). They are ETSI material, so they are
never stored in git. The build script downloads the standard from etsi.org
once and extracts them into `modules/lib/openacelp/build/`. etsi.org often
refuses scripted downloads (HTTP 403). The build then prints a warning and
continues without speech. In that case, download the ZIP in a web browser:

<https://www.etsi.org/deliver/etsi_en/300300_300399/30039502/01.03.03_60/en_30039502v010303p0.zip>

Put it in your workspace folder, next to `OpenRTX`, and run the same
`docker run` command again. It is found automatically and checked against
its SHA-256. A ZIP elsewhere in the workspace can be passed with
`-e ETSI_ZIP=/work/path/to/en_30039502v010303p0.zip` (the workspace is
`/work` inside the container). **A firmware built with the codebooks
contains ETSI material: keep it for yourself, do not distribute it.**

**Build options** go after the image name, for example:

```bash
docker run --rm -it --user "$(id -u):$(id -g)" \
    -v "$PWD":/work openrtx-c62 c62-build -DC62_REG43_SWEEP=ON
```

| Option | Default | Effect |
|---|---|---|
| `C62_WIDE_RX` | `ON` | M17-mode RX uses BK4819 `REG_43 = 0x7e28`, the widest documented filter (see section 4) |
| `C62_BASEBAND_DUMP` | `ON` | Send raw 48 kHz RX audio over the UART. The console log is muted after audio starts |
| `C62_BASEBAND_DUMP_BAUD` | `115200` | UART baud rate during the dump |
| `C62_BASEBAND_DUMP_BURST` | `ON` | Record 1 MiB (10.4 s) to PSRAM, send it, repeat |
| `C62_BASEBAND_DUMP_FORMAT` | `1` | `1` = 16-bit samples, `2` = 12-bit packed (13.9 s per burst) |
| `C62_TETRA` | `ON` | TETRA receive mode "T" (section 5), with libtetra |
| `C62_TETRA_SPEECH` | `AUTO` | TETRA speech: on when the ETSI codebooks exist (section 1) |
| `C62_REG43_SWEEP` | `OFF` | Cycle `REG_43` through 7 test values in M17 RX |
| `C62_REG43_SWEEP_MS` | `1400` | Recorded time per sweep value (one burst covers all 7 values) |

CMake remembers options in `build_c62`. To drop one, pass it again with its
default value or delete `OpenRTX/build_c62`.

Notes:

- Windows: run this from a WSL2 shell, with the folder inside WSL.
- Apple silicon: the image builds natively for arm64.
- Without Docker: the same steps are in `build.sh`. Install the Zephyr SDK
  0.16.8 `arm-zephyr-eabi` toolchain, `west`, `cmake`, `ninja`, `gperf`,
  `dtc` and `meson`, then follow the script.

## 2. Flash

Use [`cskburn`](https://github.com/LISTENAI/cskburn/releases) over the
Kenwood programming cable. To enter burn mode: radio off, hold the lower
side key, power on, release the key. The screen stays dark. Don't press the
main PTT while flashing: it shares the boot ROM's UART RX line. `cskburn`
resets the radio after each command, so enter burn mode again before each
one.

```bash
# Once, before anything else: back up your radio's own flash (it is per-unit)
cskburn -s /dev/ttyUSB0 -C 6 -b 115200 -v --read 0x0:0x400000:backup.bin

cskburn -s /dev/ttyUSB0 -C 6 -b 115200 -v --verify-all \
    0x100000 OpenRTX/build_c62/zephyr/dsp_firmware.bin \
    0x000000 OpenRTX/build_c62/zephyr/zephyr.bin
```

## 3. Capture the RX baseband

The dump uses the UART TX line on the Kenwood connector, which is the line
your programming cable receives on. Put the radio in **M17 mode** on the
wanted frequency. M17 mode applies the flat audio path: AF bypass, no
emphasis, no DC filters and the wide `REG_43`. Then:

```bash
pip install pyserial
python3 OpenRTX/scripts/c62_bbdump.py -p /dev/ttyUSB0 -t 200 -o tetra
```

A 115200-baud cable moves about 11.5 kB/s, and 48 kHz 16-bit audio is
96 kB/s, so the radio works in bursts. It records 10.4 s without gaps into
PSRAM, then takes about 91 s to send them. Recording starts again once the
burst is sent. `-t 200` gets about two bursts. Each burst becomes its own
file set:

- `tetra_burst001.wav`: 48 kHz mono 16-bit, for listening, sox or Audacity.
- `tetra_burst001.f32`: float32 in [-1, 1), for a GNU Radio *File Source*
  (float, 48 kHz).
- `tetra_burst001.csv`: per 5 ms packet, the sample index, `REG_43` value,
  RSSI (dBm) and flags.

The summary line reports peak and RMS level and a count of clipped samples.
**Check that clipping is 0 on a real TETRA signal.** Its discriminator output
swings much further than the ±2 kHz test tones. If it clips, lower the BK4819
RX gain (`REG_48`, in `m17Fields` in `radio_C62.cpp`) or `C62_ADC_RX_GAIN_*`
in `hwconfig.h`.

Use `--raw capture.bin` to also keep the raw bytes, and `-i capture.bin` to
parse them again later. Stray bytes from the boot log or the DSP are skipped
by resynchronising on the packet header and checking a CRC. The packet format
is documented in `platform/drivers/audio/baseband_dump_c62.h`.

With a faster USB-UART adapter (CH340, FT232R, CP2102N), build with
`-DC62_BASEBAND_DUMP_BURST=OFF -DC62_BASEBAND_DUMP_BAUD=1500000` for a
continuous stream. Pass the same `-b` to the script. 921600 baud is enough
with `-DC62_BASEBAND_DUMP_FORMAT=2`.

## 4. RX bandwidth (BK4819 REG_43)

From the BK4819 V3 register list:

| Bits | Field |
|---|---|
| `<14:12>` | RF (channel) filter bandwidth: 1.7, 2.0, 2.5, 3.0, 3.75, 4.0, 4.25, 4.5 kHz |
| `<11:9>` | Same filter, used instead when the signal is **weak** |
| `<8:6>` | AF TX LPF2 (TX only) |
| `<5:4>` | BW mode: 0 = 12.5 kHz, 1 = 6.25 kHz, 2 = 25 kHz. Bit 5 doubles the filter bandwidth and the IF |
| `<3>` | Undocumented, set in every known firmware |
| `<2>` | +6 dB after the FM demodulator |

The measured `0x7828` (25 kHz mode) left `<11:9>` at 3.75 kHz. The chip then
narrows the channel filter on weak signals, which is when a wide-band digital
signal can least afford it. `C62_WIDE_RX` uses `0x7e28` (4.5 kHz × 2 in both
cases), the widest documented setting. Above that are only undocumented bits.
`C62_REG43_SWEEP` measures them with your multitone setup. Each burst then
holds 1.4 s each of `7808 7828 7e28 7e2c 7e38 7e20 fe28`. Split them with
`c62_bbdump.py ... --split-reg43` and skip about 50 ms at each value change.

## 5. TETRA mode

Press **5** on the main screen to cycle the modes: FM, M17, T. In **T**
mode the radio receives a TETRA TMO downlink (a base station, pi/4-DQPSK)
and decodes it on the radio with
[libtetra](https://github.com/sp5wwp/libtetra). It is receive only: PTT does
nothing. Tune to the base station's downlink frequency as usual.

The main screen shows:

| State | Line 1 | Line 2 | Rest |
|---|---|---|---|
| No signal | Demodulator figures | `T searching` | Frequency |
| Locked to a cell | Last SDS text (scrolling, 30 s) or demodulator figures | `T <MCC>-<MNC> LA <n>` | Frequency |
| Call | Talking party SSI | Called group (`G`) or individual SSI | Cell, then `Voice` / `Idle` / `Encrypted` |

The green LED is on while the radio is locked to a cell. A call stays on
screen for 10 s after its last activity unless its release is decoded.
Speech starts 150 ms after the first traffic slot: TETRA leaves a gap of
113 ms in every multiframe (frame 18), which the buffer must cover. A
dash means the network has not sent that identity since the radio started
listening, typically because the call was set up before.

The demodulator figures on line 1, e.g. `e11% -234Hz o0 r1`:

- `e`: RMS phase error in % of pi/4. About 10 to 20% on a clean signal;
  decoding fails above about 45%, where the demodulator starts again.
- the carrier offset in Hz.
- `o`: overruns, blocks of samples lost because the radio could not keep
  up. This should stay at 0: if it counts up, the radio is short of CPU
  time, and speech will break up.
- `r`: restarts of the burst synchronisation: at start, after an overrun,
  or after the demodulator followed a jump in the signal's timing. A
  replayed recording causes one at every loop.

How it works: the BK4819 runs the same flat, wide receive path as M17
(`REG_43 = 0x7e28`, AF bypass). The 48 kHz discriminator output goes to
`openrtx/src/protocols/TETRA/TetraDemod.c`: integrate-and-dump over each
symbol, a timing loop for the sample clock offset, a running fit of gain
and carrier offset, and a 9-tap equalizer seeded with taps measured on the
C62 (`CONFIG_TETRA_EQ_INIT` in `hwconfig.h`). Its soft bits go to libtetra.

Speech: with the ETSI codebooks (section 1), clear (not encrypted) speech
is played on the speaker. libtetra decodes each traffic slot into two
speech frames, and [OpenACELP](https://github.com/sp5wwp/OpenACELP) decodes
them into 8 kHz audio. The speaker is released after one second without
speech. The build log says `C62: TETRA speech enabled` or `disabled`.
`-DC62_TETRA_SPEECH=OFF` turns it off even with the codebooks present.

The baseband dump keeps working in T mode, to capture what the radio
receives.
