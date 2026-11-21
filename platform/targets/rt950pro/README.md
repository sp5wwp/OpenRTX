<!--
 - SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 -
 - SPDX-License-Identifier: GPL-3.0-or-later
-->

# Radtel RT-950 Pro

This target is based on the Artery AT32F403A (Cortex-M4F, 120 MHz, 1MB flash,
224kB SRAM once EOPB0 is set, see [Memory](#memory)). It runs on Zephyr, using
the Artery fork of Zephyr and the `hal_at32` module (`artery` group of
`west.yml`).

The hardware description comes from the reverse engineering work done in
[Radtel-RT950-Pro-Firmware](https://github.com/jaymart1983/Radtel-RT950-Pro-Firmware).

## Hardware

| Component        | Description                                |
|------------------|--------------------------------------------|
| MCU              | Artery AT32F403A, ARM Cortex-M4F           |
| Transceivers     | 2x BK4829                                  |
| Broadcast RX     | SI4732 (FM/AM/SSB)                         |
| Display          | ST7789V, 240x320                           |
| External Flash   | Zbit ZB25VQ32 (JEDEC 5E4016), 4MB on SPI2, vendor data and calibration |
| GPS              | ATGM336H                                   |
| Bluetooth        | BLE module, transparent UART link          |

## Status

Supported:

- display: ST7789V 240x320 panel on a bit-banged 8-bit 8080 bus. The OpenRTX
  UI is drawn in a 160x128 frame, scaled by 1.5 to 240x192 and centered on the
  panel. The unused 240x128 area at the top of the panel shows the OpenRTX
  logo and the callsign at boot, drawn row by row directly to the panel
  without using the framebuffer. The backlight brightness is adjustable from the OpenRTX settings
  (PWM on PC6, TMR8 channel 1). The keypad backlight (PB3) is switched on and
  off together with the display one.
- keypad, side keys, PTT and rotary encoder
- serial logs (Zephyr console) on UART4, at 115200 baud, on the programming
  cable
- power switch: the radio is switched off when the knob is turned off
- battery voltage: ADC1 channel 1 (PA1) through a resistive divider
- GPS: NMEA at 9600 baud on USART3 (PB10 TX, PB11 RX), powered by PA8
  (active low)
- time/date: the AT32 hardware RTC, using the existing 8 MHz main crystal
  divided by 128 (62,500 Hz): the board has no 32.768 kHz crystal (the LEXT
  oscillator never starts). The RTC keeps time while the firmware runs, but
  stops with the main oscillator: while the bootloader flashes, and when the
  radio is switched off. Its divider is write-only, so it is written at every
  start. Manual setting and optional GPS time synchronization use the existing
  OpenRTX settings; `re-tools/rtcsync.py` sets it from the PC.
- NVM settings storage in last 8kB of internal flash, see [Memory](#memory)
- BK4829 transceivers: both chips are reset and initialised at boot. They
  share a Zephyr SPI bit-banged, half-duplex 3-wire serial bus at 100 kHz
  (SCLK PE10, SDATA PE11); chip 0 (PE8
  chip select) is wired to the VHF LNA and PA, chip 1 (PE15) to the UHF ones.
  Both chips use one generic OpenRTX SPI device with a shared transaction
  mutex; the BK4829 driver controls chip select across command and data.
  The Zephyr SPI adapter performs transfers directly, without a separate
  bus bridge module.
- RF reception, for testing only: `radio_rt950pro.cpp` tunes the VHF
  (136-174MHz) or UHF (400-470MHz) chip and reports the RSSI to the UI. The
  receive RF path switches (PE13, PE12, PB0) are controlled according to the
  selected band. Transmit path switches (PE7, PB1, PE14) and the band relay
  (PC4) remain untouched.
- audio: see [Audio](#audio). FM receive audio to the speaker, and the
  baseband for digital modes: DAC1 output (Codec2 playback) and ADC3 input
  (M17 demodulation), with a receive noise canceller.
- M17 receive path up to the demodulator, and Codec2 (Codec2-mod) decoding to
  the speaker. Reception of a real M17 signal is not validated yet.
- GPS: the ATGM336H answers when powered, and its NMEA sentences are parsed
  (minmea); a position fix is not validated yet.
- transmission, UHF and VHF, with the vendor key-up sequence and the factory
  power calibration of the external flash (see [Transmission](#transmission)).
  Verified on UHF at the low power level with an SDR. M17 transmissions
  decode with gr-m17 (see [M17 transmission](#m17-transmission)).
- SI4732 driver (FM only)
- Bluetooth BLE module on USART1 (TX PA9, RX PA10), driven by
  `bluetooth_rt950pro.c` as a transparent data link. The module is enabled by
  PB9 (high) and its UART runs at 9600 baud.
  

Not supported yet:

- microphone capture for digital modes: PA0 only carries a rectified mic level
  (VOX), so there is no known ADC path for the microphone audio; M17
  transmissions use a bench clip
- BK4829 GPIO driver (not required for RT950 pro)
- SI4732 AM/SSB reception
- Bluetooth core integration (CPS over Bluetooth, menu) and audio


## Audio

All the receive audio goes through one analog node, `VOICE_IN`: the AF
outputs of both BK4829 chips are summed there, the speaker amplifier listens to
it, and PA2 (ADC3) reads it. DAC1 (PA4) drives the same node when PC12 is low,
or the modulators' microphone input (`MICin`) when PC12 is high. So the MCU
cannot play audio to the speaker without the receiver's audio, nor sample the
receiver without its own audio: the receive canceller deals with that.

Speaker amplifier (TDA2822): powered by PE4, enabled by PB8, muted by PE1
(high), PE9 low selects the speaker rather than Bluetooth. `audio_rt950pro.c`
enables it while any route plays to the speaker.

- `drivers/audio/dac_at32.c`: TMR6 interrupt at 48 kHz, one DAC1 sample per
  interrupt, stream sample rates dividing 48 kHz, linearly interpolated.
  Stream samples go through a look-ahead peak limiter (8 ms): speech is scaled
  up, loud peaks are lowered smoothly instead of clipped, and the gain never
  steps (a step is a click). Up to 4 per-sample hooks (test generator,
  canceller, recorder). Kernel calls are deferred to a software-triggered
  interrupt (TMR7 line), so the DAC interrupt stays short and regular.
- `drivers/audio/adc_at32.c`: ADC3 on PA2, one conversion per DAC interrupt,
  averaged down to the stream rate, DC removed. Input streams of the
  `SOURCE_RTX` path (M17 demodulator, 24 kHz).
- `canceller.c`: a model of the DAC contribution to the node (6+8 coefficient
  IIR), removed from the ADC samples, so that the demodulator sees the
  receiver alone; and a 32-tap filter predicting the receiver noise from them,
  added to the DAC output, so that it cancels on the node and at the speaker.
  It runs while the receiver goes to the MCU but not to the speaker (digital
  modes). Measured: -14 dB of receiver noise on the node, about 15 to 17 dB at
  the speaker, voice-to-noise about 32 dB at full volume with no signal. The
  model depends on the timing of the DAC interrupt: measure it again
  (`re-tools/canceller_train.py`) after changing that. A NaN or overflow in its
  state resets it.
- M17 receive: the BK4829 runs in AF bypass mode, with the receive gains at
  `REG_48` = 0xB0A1, which keeps the receiver noise low on the node.

The interrupt path runs from Flash in the `.audio_isr` section (zero-wait
area), without library calls; the DAC interrupt takes about 46% of the CPU
during playback with the canceller on.

## Interrupts and timing

- The vendor bootloader leaves the interrupt priority grouping at 7 (no
  preemption bits): no interrupt preempts another. `board_init.c` restores 0.
  Before that, the audio interrupt waited up to a whole sample behind other
  handlers, which the canceller turned into clicks at the speaker.
- Priorities: the DAC interrupt (TMR6) and the programming cable UART (UART4)
  share the highest one. The UART must not be preempted by the audio
  interrupt: when it was, its received characters were corrupted. USART1
  (Bluetooth), USART3 (GPS) and ADC1 (battery voltage) are below, the kernel
  timer below them. A handler at the audio priority holds the audio interrupt
  off until it ends: ADC1 at that priority made it lose about 11 samples per
  second (the battery voltage is read about ten times per second). Each lost
  sample slips the audio output by one sample: on M17 transmissions a symbol
  timing sawtooth (36 us rms), and on the bench an apparent MCU clock error of
  -146 to -210 ppm, varying with the battery reads. The crystal is within
  about 5 ppm.
- The DAC driver counts the lost samples (`dacAt32_lostTicks`, from the TMR6
  counter at the interrupt entry), the longest interrupt latency
  (`dacAt32_latencyMax`), the half buffers output before their refill
  (`dacAt32_lateRefills`) and the samples clipped (`dacAt32_clipped`). Each
  debug monitor command still costs a few lost samples (UART4 at the audio
  priority).
- The debug monitor thread has the lowest priority: long replies (memory
  dumps) used to delay the M17 transmit thread, which then refilled the audio
  buffer late, and stale samples were transmitted.
- With 224 kB of SRAM only the first 128 kB of Flash are zero-wait, and code
  above runs many times slower. `rt950pro_hot_code.ld` places the kernel, the
  ARM interrupt and context switch code, the timer, serial and GPIO drivers and
  the runtime libraries there, after the audio interrupt code. Before, the
  slow interrupt-masked kernel sections made the audio interrupt miss about
  650 ticks per second, and the serial receive interrupt overrun.

## Memory

- SRAM: the AT32F403A has 96 kB, or 224 kB with the EOPB0 option byte set,
  which `platform_init()` does once (battery above 6.8 V), then reboots; the
  Codec2 decoder does not fit otherwise. `CONFIG_SRAM_SIZE=224` and the
  `sram0` node must match it. The radio comes with the Flash access protection
  (FAP) on: never write 0xA5 to FAP, that unprotects it and mass-erases the
  Flash, the vendor bootloader included. `optionbytes.c` never touches FAP.
- Internal Flash: vendor bootloader (12 kB, never overwritten), application
  from 0x08003000, OpenRTX settings and VFO in the last 8 kB (0x080FE000), two
  sectors with a magic value and a CRC each. The bootloader rewrites only the
  application: the settings survive a firmware update.
- External SPI NOR (SPI2: PB13 SCK, PB14 MISO, PB15 MOSI, PB12 CS): Zbit
  ZB25VQ32, 4 MB, read by the Zephyr `jedec,spi-nor` driver (geometry from
  SFDP). OpenRTX only reads it. The vendor firmware keeps its data there,
  the transmit power calibration included; `re-tools/nordump.py` dumps it.

## Transmission

`radio_rt950pro.cpp` keys the transmitter as the vendor firmware does
(V0.27; the sequence, with the addresses of the vendor routines, is in the
reverse engineering notes):

1. PA drive (APC, DAC2 on PA5) at zero, receive RF paths off;
2. transmit RF path on: PE7 on UHF, PB1 on VHF. PE14 (HF) and the PC4 band
   relay are never touched;
3. BK4829: frequency, bandwidth, no sub-tone, microphone gain 0x24,
   deviation 0x50A, keyed (`REG_30` = 0xC1FE), PA gains (`REG_36` 0x7F high,
   0x57 mid and low);
4. PA drive ramped up to the factory calibration in 4 steps, 1 ms apart.

Key-down: PA drive to zero, 100 ms, chip out of TX, transmit path off. A
receive path is never on together with a transmit path. There is no
transmission on a frequency without a programmed calibration point.

Power levels: the OpenRTX power setting maps to the calibration levels:
5 W and up high, 2 W and up mid, below low. The calibration (SPI NOR
0xF000) holds a PA drive byte per level (0xF000 high, 0xF040 mid, 0xF080
low), band row (+0x00 UHF, +0x10 VHF, +0x20 200-300 MHz, +0x30 350-400 MHz)
and frequency point (UHF: (MHz - 400) / 10; VHF: (MHz - 130) / 3); 0x00 or
0xFF is not programmed.

Measured on 433.475 MHz, low power, with an RTL-SDR: frequency within the
SDR tolerance (+390 Hz), spurious below -62 dBc within 1.2 MHz, residual FM
about 3 Hz rms. A PA drive of 0x10 or less gives about 35 dB below the low
level: the PA is not off with a zero drive.

### M17 transmission

The modulation goes from DAC1 to the microphone input of the BK4829 chips
(`MICin`, PC12 high), with the chips' TX audio filters bypassed. That path
behaves as a first order band-pass, a high-pass at 34 Hz and a low-pass at
170 Hz (sine tones from 2 Hz to 4 kHz on the DAC, against the deviation
measured with an SDR, 0.3 dB rms fit). The DAC driver equalises it in its
linear mode with two first order sections (`audio_rt950pro.c`): a zero at
170 Hz with a pole at 8 kHz, and the high-pass undone down to 1 Hz. In
linear mode the DAC converts at each TMR6 overflow (hardware trigger): the
interrupt latency (up to about 18 us) no longer moves the samples in time.

The deviation is set by the DAC level (`TX_LEVEL`, DAC peaks at about 80 % of
the range) and by the BK4829 deviation register, 0x5C2 for M17 (0x50A, the
vendor value, for FM): outer symbols at +/-2.4 kHz, a preamble peak of about
3.4 kHz (1.41 times the outer symbols, with the RRC filter). The transmit
phase is inverted, as on the C62.

The radio has no microphone input for the MCU (PA0 only carries a rectified
level for VOX), so the Codec2 encoder takes its audio from a clip loaded in
RAM from the debug monitor (`benchmic.c`, `mic` commands), played in a loop.

Verified with an RTL-SDR capture (FM demodulator, RRC matched filter, fixed
symbol timing per 0.5 s) decoded by gr-m17: LSF (source, destination, voice
stream, meta text), 120 stream frames over 5 s, Viterbi error metric 9-12
(decoder threshold 30), and the Codec2 payload decodes to the speech of the
clip. The modulation error ratio (MER) is about 28 dB; the receive chain
measures 33 dB on an ideal synthetic signal, and the DAC output itself about
32 dB. On the way there:

| Step | MER |
|------|-----|
| start: 230 Hz equaliser, samples lost by the audio interrupt | 13.7 dB |
| sample rate corrected for the apparent clock error | 17.3 dB |
| 28 Hz high-pass undone | 20.8 dB |
| ADC1 interrupt below the audio one, DAC hardware trigger | 22.8 dB |
| path measured with tones: 170 Hz / 34 Hz, high-pass undone to 1 Hz | 27.9 dB |

The M17 transmission depends on the CPU keeping up: the Codec2 encoder, the
modulator and their tables are in the zero-wait Flash, and the modulator
shapes the symbols in polyphase form.

## Bench tools

The firmware includes a debug monitor on the programming cable (UART4, 115200
baud, shared with the Zephyr log: log lines start with `[`). One command per
line, answered by its output then `OK`, or `ERR <reason>`:

| Command | Function |
|---------|----------|
| `ping` | answers `RT950PRO` |
| `rb`/`rw`/`wb`/`ww` | read/write memory and registers |
| `update` | restart into the vendor bootloader, for flashing |
| `xr`/`xw <v\|u> <reg> [value]` | read/write a BK4829 register (VHF/UHF chip) |
| `mode <fm\|m17>`, `freq <hz>` | operating mode and frequency |
| `tg <shape> [hz] [amp]` | test signal on the DAC (0 off, 1 sine, 2 square, 3 noise), hex values |
| `ar <channel> <count> [div]` | record an ADC channel with the DAC output |
| `cx <0\|1>` | receive canceller correction off/on |
| `c2 s/p/l/m/e` | Codec2 playback: start, push frames, load a clip into RAM, play it, end |
| `sf id`, `sf r`, `sf c` | external flash: JEDEC ID, read, CRC-32 (read only) |
| `ptt <ms>`, `ptt off` | transmit through the normal path, released after at most 10 s |
| `txdry <0\|1>` | dry run: key the transmitter with the PA drive at zero (default 1) |
| `txdrv <value>` | PA drive below the calibration, 0 for the calibration |
| `txlvl <gain>` | modulation level on DAC1, 1/256 units (hex) |
| `clk <error>` | correct the audio sample rate for an MCU clock error (0.1 ppm, decimal) |
| `cal` | measure the MCU clock against a BK4829 tone (`clockcal.c`), apply it, print it |
| `mic a <n>`, `mic l <off> <hex>` | bench microphone clip: allocate, load 16-bit samples |
| `rtc`, `tz` | RTC date and time (UTC), time zone |
| `ob` | option bytes (SRAM size) |
| `hp` | largest free heap block |

The bootloader handshake of the vendor update tool, and the bootloader probe
frame, are recognised in the UART interrupt: the radio restarts into the
bootloader, so it can be flashed without pressing keys.

`re-tools/` (Python, run from that folder):

- `build.sh`: build in the Docker image (`PRISTINE=1` for a clean build, needed
  after device tree changes that Kconfig defaults derive from);
- `reflash.sh <image.BTF>`: flash without keys, then set the RTC from the PC;
- `monitor.py <command>`: run one monitor command;
- `rtcsync.py`: set the RTC and time zone from the PC;
- `nordump.py <out.bin> [start] [length]`: dump the external Flash, every 4 kB
  checked against the CRC computed by the radio;
- `canceller_train.py [--write]`: measure the receive canceller model and
  filter on the radio (M17 mode, no signal, amplifier powered off), and put
  them in `canceller.c`.

### Key mapping

| Radio key            | OpenRTX key  |
|----------------------|--------------|
| OK                   | `KEY_ENTER`  |
| Back                 | `KEY_ESC`    |
| Arrows               | Arrows       |
| ABC                  | `KEY_F1`     |
| V/M                  | `KEY_F2`     |
| Top side key         | `KEY_MONI`   |
| Bottom side key      | `KEY_F3`     |
| Encoder              | `KNOB_LEFT` / `KNOB_RIGHT` |

## Building

Requirements:

- Latest OpenRtx zephyr devcontainer which this repo is already based on
- the Python packages needed by Zephyr 4.3, installed once after the first
  container start (need to start the build once to download zephyr-artery):
  `uv pip install -r <west topdir>/zephyr-artery/scripts/requirements-base.txt`.

The target is built via `meson` for convenience:

```bash
meson setup build
meson compile -C build openrtx_rt950pro
```

Meson only runs `west update --group-filter +artery`, to fetch the Artery
Zephyr fork, `hal_at32` and `CMSIS_6`, and then runs `west build`, which
invokes CMake to configure and build the firmware in the `rt950pro` subfolder
of the meson build folder.

- `build/rt950pro/zephyr/zephyr.bin`: raw image, linked at 0x08003000;
- `build/rt950pro/openrtx_rt950pro.BTF`: flash image for the vendor
  bootloader.

The firmware can also be built directly with west, without meson:
```bash
export ZEPHYR_BASE=<west topdir>/zephyr-artery
west update --group-filter +artery
west build -b rt950pro -d build_rt950pro .
```


## Flashing

> **Warning:** This may brick your device! Use at your own risk!

The vendor bootloader (first 12kB of flash) is never overwritten, the original
firmware can be restored at any time with the official update tool.

1. Switch the radio off.
2. Turn the radio on while holding both side keys: the bootloader enters the
   update mode.
3. Connect the programming cable and flash `openrtx_rt950pro.BTF` with the
   `rt950_flash.py` script:

```bash
python3 scripts/rt950_flash.py upload /dev/ttyUSB0 build/rt950pro/openrtx_rt950pro.BTF --ptt
```

## Serial logs

Connect the programming cable and open its serial port at 115200 baud, 8N1:

```bash
python3 -m serial.tools.miniterm /dev/ttyUSB0 115200
```
