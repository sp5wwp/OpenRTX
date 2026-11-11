<!--
 - SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 -
 - SPDX-License-Identifier: GPL-3.0-or-later
-->

# Radtel RT-950 Pro

This target is based on the Artery AT32F403A (Cortex-M4F, 1MB flash, 96kB
SRAM). It runs on Zephyr, using the Artery fork of Zephyr and the `hal_at32`
module (`artery` group of `west.yml`).

The hardware description comes from the reverse engineering work done in
[Radtel-RT950-Pro-Firmware](https://github.com/jaymart1983/Radtel-RT950-Pro-Firmware).

## Hardware

| Component        | Description                                |
|------------------|--------------------------------------------|
| MCU              | Artery AT32F403A, ARM Cortex-M4F           |
| Transceivers     | 2x BK4829                                  |
| Broadcast RX     | SI4732 (FM/AM/SSB)                         |
| Display          | ST7789V, 240x320                           |
| External Flash   | SPI 4MB, contains config, 64 KiB read back |
| GPS              | ATGM336H                                   |
| Bluetooth        | HC-05                                      |

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
  divided by 128 (62,500 Hz). Manual setting and optional GPS time
  synchronization use the existing OpenRTX settings.
- NVM settings storage in last 8kB of internal flash
- BK4829 transceivers: both chips are reset and initialised at boot. They
  share a Zephyr SPI bit-banged, half-duplex 3-wire serial bus at 100 kHz
  (SCLK PE10, SDATA PE11); chip 0 (PE8
  chip select) is wired to the VHF LNA and PA, chip 1 (PE15) to the UHF ones.
  Both chips use one generic OpenRTX SPI device with a shared transaction
  mutex; the BK4829 driver controls chip select across command and data.
  The Zephyr SPI adapter performs transfers directly, without a separate
  bus bridge module.
  The driver is not validated on hardware yet.
- RF reception, for testing only: `radio_rt950pro.cpp` tunes the VHF
  (136-174MHz) or UHF (400-470MHz) chip and reports the RSSI to the UI. The
  receive RF path switches (PE13, PE12, PB0) are controlled according to the
  selected band. Transmit path switches (PE7, PB1, PE14) and the band relay
  (PC4) remain untouched.
- RX audio: `audio_rt950pro.c` routes the receiving BK4829 to the speaker. At
  boot the speaker is muted (PE1 high), the amplifier is powered (PE4), the
  audio source switch selects the BK4829 chips (PC12 high) and the Bluetooth
  switch the speaker (PE9 low). Connecting the transceiver to the speaker
  enables the amplifier (PB8) and unmutes it, following the squelch. The
  chip which is not receiving has its AF registers cleared, as both chips share
  the same audio bus. The routing comes from the reverse engineering work and is
  not validated on hardware yet.
- SI4732 driver (FM only)

Not supported yet:

- RF transmission
- BK4829 GPIO driver (not required for RT950 pro)
- SI4732 AM/SSB reception
- Audio routing transmission
- Baseband Audio access (to support digital modes)
- External flash (e.g. calibration, CPS reading)
- Bluetooth


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
