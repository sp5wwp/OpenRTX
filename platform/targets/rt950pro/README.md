<!--
 - SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 -
 - SPDX-License-Identifier: GPL-3.0-or-later
-->

# Radtel RT-950 Pro

This target is based on the Artery AT32F403A (Cortex-M4F, 1MB flash, 224kB
SRAM, selected at the first start by the EOPB0 option byte, see
`optionbytes.c`). It runs on Zephyr, using the Artery fork of Zephyr and the `hal_at32`
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
| External Flash   | SPI 4MB, holds the factory calibration    |
| GPS              | ATGM336H                                   |
| Bluetooth        | BLE module, transparent UART link          |

## Status

Supported:

- display: ST7789V 240x320 panel on a bit-banged 8-bit 8080 bus. The OpenRTX
  UI covers the whole panel, drawn 80 rows at a time instead of in a
  framebuffer, which would leave too little heap for M17. The backlight
  brightness is adjustable from the OpenRTX settings (PWM on PC6, TMR8
  channel 1); the keypad backlight follows it (PWM on PB3, TMR2 channel 2,
  remap 1).
- keypad, side keys, both PTT buttons and rotary encoder
- serial logs (Zephyr console) on UART4, at 115200 baud, on the programming
  cable
- power switch: the radio is switched off when the knob is turned off
- battery voltage: ADC1 channel 1 (PA1) through a resistive divider
- GPS: NMEA at 9600 baud on USART3 (PB10 TX, PB11 RX), powered by PA8
  (active low)
- time/date: the AT32 hardware RTC, using the existing 8 MHz main crystal
  divided by 128 (62,500 Hz). Manual setting and optional GPS time
  synchronization use the existing OpenRTX settings.
- NVM settings storage in last 8kB of internal flash, written 3s after the
  settings or the VFO last changed, so a power loss or an update keeps them
- BK4829 transceivers: both chips are reset and initialised at boot. They
  share a Zephyr SPI bit-banged, half-duplex 3-wire serial bus at 100 kHz
  (SCLK PE10, SDATA PE11); chip 0 (PE8 chip select) is wired to the VHF LNA
  and PA, chip 1 (PE15) to the UHF ones. Both chips use one generic OpenRTX
  SPI device with a shared transaction mutex; the BK4829 driver controls chip
  select across command and data.
- bands: 136-174MHz and 222-225MHz on chip 0, 400-480MHz on chip 1. The
  target lists them in `CONFIG_RF_BANDS_MHZ` and `radio_rt950pro.cpp` has a
  row for each: chip, receive and transmit path switch, calibration row. The
  band relay (PC4) and the HF paths remain untouched.
- FM: receive and transmit, 12.5kHz and 25kHz, CTCSS transmit at the level of
  the factory calibration, the 1750Hz tone burst.
- M17 voice, receive and transmit, done on the MCU: neither chip gives a
  baseband. To transmit, the band's chip sends its tone generator, whose gain
  is the baseband (`BK4829_tone.cpp`), and the other chip passes the
  microphone to the MCU's ADC. To receive, the band's chip is tuned 8kHz below
  the signal and its mixer output is demodulated from the ADC (`at32_adc.c`);
  the speech is played from DAC1 (`at32_dac.c`). Codec2 is Codec2-mod, for its
  smaller memory use. The receiver's IF is audible as a hiss behind the
  speech: the chips' audio output and DAC1 share the net the speaker is fed
  from. The IF's AF level steps down during a call while the frames decode
  cleanly, which lowers the hiss on strong signals. Frames are decoded from
  soft symbols; a stream frame is accepted on its Viterbi path metric, set so
  that almost no wrong decode is played (see below).
- M17 text messages, with an inbox of 8 entries: the heap has no room for
  more beside M17 voice.
- transmit power: 1, 5 and 10W from the factory calibration in the external
  flash (`nvmem_rt950pro.c`), read only. On 222-225MHz the three settings give
  the same output.
- beeps, from the chip that is not receiving.
- audio routing: `audio_rt950pro.c`. PE1 high mutes the speaker, PE4 powers
  the amplifier, PB8 high takes the microphone off its line, PC12 selects the
  BK4829 chips (high) or DAC1 (low) and PE9 the speaker (low) or Bluetooth.
  The volume knob is analog, after these: the firmware cannot read it.
- watchdog: started by the firmware, about 4s. A fatal error restarts the
  radio.
- SI4732 driver (FM only)
- Bluetooth BLE module on USART1 (TX PA9, RX PA10), driven by
  `bluetooth_rt950pro.c` as a transparent data link. The module is enabled by
  PB9 (high) and its UART runs at 9600 baud.
  

Not supported yet:

- BK4829 GPIO driver (not required for RT950 pro)
- SI4732 AM/SSB reception
- HF: the band relay and the HF receive and transmit paths
- CTCSS receive and voice prompts: in the code, not checked on a radio yet
- Channels: the codeplug is not stored
- Time while the radio is off: there is no 32kHz crystal, GPS sets the time
- Bluetooth core integration (CPS over Bluetooth, menu) and audio


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

## Bench findings

Measured on one radio with a USRP B210 at 433.475MHz, the radio through a
40dB attenuator. The B210's reference was itself measured against GSM base
stations (`re-tools/b210_gsm_cal.py`): it runs 2.35ppm fast. The tools are in
`re-tools/`; `monitor.py` talks to the debug monitor on UART4 (`monitor.c`).

M17 transmitter (`BK4829_tone.cpp`):

- The chip reads the tone gain 26MHz / 512 times a second, on its own
  crystal. Written at another rate, the writes' images alias into the
  baseband at the difference of the rates minus the baseband frequency:
  2781Hz - f for writes at 48kHz (a 2kHz test tone showed its image at 782Hz,
  -31dBc). The writes now follow the chip's rate, 50.28 bus slots of 392ns
  apart, each with the level of its own time. With a whole number of timer
  cycles per write, 33ppm off, the chip's reads sweep through the writes a
  few times a second: a sawtooth of the delay, MER 11dB. 142ns slots lose DMA
  transfers to GPIOE.
- The tone path is a first-order 7.9Hz high-pass (a step of the gain decays
  in 20.1ms), compensated on the baseband.
- The gain curve's 128 codes are as assumed, to 0.15 units rms; 42.4Hz of
  deviation a unit. The deviation stops growing 280 units above code 0
  (3.8kHz over the old zero level of 190): the zero is now 156.
- In the band, the path is flat to 2.4kHz, 2dB down at 2.8kHz and 13dB at
  3.2kHz. The pulse is designed by least squares against that response for
  no intersymbol interference after the receiver's RRC; the excess bandwidth
  lost at 3.2kHz is made up from its fold below 2.4kHz.
- Each gain write takes the nearest code and carries its error into the next
  write: first order noise shaping.
- Together: MER from 21.7dB to 30.7-30.9dB median (an occasional 0.5s block
  near 23dB, while the chip's reads cross the writes' slot of jitter), and
  the microphone has no path into the modulation. The `tt` monitor command
  sends test levels in place of the baseband.
- The carrier is 0.03 to 0.14ppm high.

M17 receiver:

- At B210 gain 15, 12 and 10dB, 93%, 27% and 3% of the frames sent were
  accepted. At 12dB only 193 of 324 frames reached the decoder: the sync
  loses them, which Gaussian noise in a host simulation does not reproduce.
  IF captures (`cap`, `re-tools/ifcap.py`) replayed through the receiver's
  demodulator on the host (`re-tools/sim`) explain it: at 12dB the IF is far
  below the FM threshold, about 290 clicks a second, near -1dB CNR. The
  radio's own speech on the IF's net is not the cause (`smute`).
- The IF's AF level: level 1 costs 10 to 15dB of sensitivity, level 0 fails
  on all but strong signals.
- Stream frames carry no CRC. Simulated, the path metric threshold of 23 lets
  1 wrong decode of 33000 frames through, from 0.4 to 1.0 of a symbol unit of
  noise, and keeps 94% of the right ones at 0.5 and 36% at 0.6.
- Below threshold, in the host model of the IF (500 frames, right ones
  accepted at 6/4/2dB CNR: 97.8/69.4/4.6%), neither of these helped:
  - shrinking each demodulated sample towards zero as the IF's power dips
    below its mean: it takes most of the clicks away, but bends the symbol
    levels more, and decodes fewer frames at every weight (63% at 4dB with
    the lightest);
  - a narrower or wider second IF filter: at 3.4, 3.8, 4.1 and 5kHz, 43.6,
    62.0, 66.2 and 67.8% at 4dB. The 4.4kHz filter is the best.
  Blanking clicks on the demodulated signal or gating on the IF's power
  lost frames too. Gains below threshold need another demodulator (a PLL,
  or the symbols detected straight from the IF).

Clocks:

- The CPU runs at 120MHz, as in the vendor firmware. 240MHz works and can
  be selected in `rt950pro.dts` (see the comment there), but a spur lands
  near 433.4747MHz and strong-signal M17 reception drops from 323 to 279
  frames of 324, so it is not the default. The Artery clock driver sets
  APB dividers of 4 as 8; `board_init.c` corrects them.

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
