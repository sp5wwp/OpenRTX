<!--
SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors

SPDX-License-Identifier: GPL-3.0-or-later
-->

# AnyTone AT-D890UV port: working notes

Proof-of-concept port, branch `d890uv`. These notes record what has been
found, how it was verified, and what is left. Unless stated otherwise,
facts come from the stock firmware **V1.05**
(`D890UV_V1.05_DMR FW/D890UV_V1.05_20260521.CDD`). Addresses below are stock
firmware addresses. "Bench-verified" means confirmed on the radio with a
signal generator and spectrum analyser.

## 1. Status

| Area | State |
|---|---|
| Boot, 160 MHz clock, display, keypad, side keys, knob | Working |
| Battery voltage (ADC) | Working, calibrated |
| FM receive, UHF and VHF | Working, factory calibration applied |
| RSSI / S-meter, squelch | Working, calibrated with a generator |
| Volume | Provisional: knob drives the WX4818N AF level `0x30`, uneven curve |
| Beeps | Disabled (see 7) |
| Transmit | Disabled: `TX_PWR_EN` (PA15) is forced low everywhere |
| M17 / digital | Not started |

## 2. Building and flashing

- Build: `meson compile -C build_cm4 openrtx_d890uv_wrap`. This makes
  `openrtx_d890uv.spi/.CDI/.CDD` with `scripts/anytone_wrap.py`. The wrap
  output is byte-identical in format to the stock update files.
- If meson hangs cloning `subprojects/XPowersLib`, clone it by hand and add
  the `packagefiles` contents.
- Flasher: <https://github.com/sp5wwp/anytone-flasher>:
  `python3 flash_anytone.py build_cm4/openrtx_d890uv.spi /dev/ttyACM0 --yes`.
- **Update mode**: power on while holding **PTT + top key** (PF3, the
  emergency key). The radio shows "UPDATE MODE". PTT + PF2 gives a
  different mode that does not install firmware.
- The stock bootloader occupies 0x08000000-0x0800BFFF. The application
  starts at 0x0800C000 (`VECT_TAB_OFFSET` 0xC000). The bootloader cannot
  be overwritten by updates, so a bad image is always recoverable.

## 3. Hardware

MCU **STM32U575VIT6** (Cortex-M33). It runs the Miosix `cortexM4F` port with
`-mcpu=cortex-m4` flags, at 160 MHz from PLL1 fed by HSI16 (`bsp.cpp`).
The pin assignments are in `pinmap.h`. The AnyTone I/O table, made for an
AT32F435 board revision, is mostly valid. The display and keypad pins differ.

- **Display**: ST7735S, frame memory 128x160, `MADCTL 0x88`. The panel is
  mounted landscape (160x128), so the driver transposes in software.
  The 8080 bus is bit-banged: D0-D7 on PE7-PE14, DC PD12, RST PD13, CS PD14,
  WR PD15.
- **Keypad**: 3x8 matrix. Columns share the LCD data lines; rows are PC4,
  PB1, PB2.
- **Keys**:
  - Upper side key PE2 = `KEY_MONI`; lower side key PA14 = `KEY_F1`;
    top key PA8 = `KEY_F3`. All active low.
  - PTT is PA10. The knob is a quadrature encoder on PC8/PC9.
- **ADC1**:
  - Battery on PC1 (IN2): 251770 uV at the pin per battery volt, plus a
    20 mV offset.
  - Volume pot on PC2 (IN3).
  - The bootloader leaves ADC1 unusable: reset it through RCC
    (`adc_stm32u5.c`).
- **Power**: PC3 power hold, PA0 power-on detect, PA15 TX power,
  PB0 backlight.
- **Audio**:
  - PC12 is the LN4898 speaker amplifier, **high = on**. The I/O table's
    "SPK MUTE H=OFF" means the mute is off when high.
  - PD0-PD2 are audio routing switches; for RX use PD2=1, PD1=0, PD0=0.
    PE0 is mic mute (high), PA1 Bluetooth power, PE1 APC power.
  - The amplifier input comes through switch U202 (RS2057), selected by
    band B chip GPO1. Its two sources are **D-VOL**, from the digital board
    (SCT3288), and **TONE**, the PB9 PWM. Stock volume control very likely
    happens on the D-VOL path, not in the WX4818N.
- **DAC1**:
  - Channel 2 (PA5) is the **UHF front-end filter tuning voltage**.
    The I/O table calls it "RXVCA"; it is not the volume.
  - Channel 1 (PA4) is TX APC.
- **RF**: two WX4818N transceivers (BK4819-like, no datasheet).
  - Band A is U200, CS PB5; band B is U219, CS PD3. SCLK PB3 and SDIO PB4
    are shared.
  - Frame: one byte `reg << 1 | R`, then 16 data bits, MSB first.
    Write/readback of register `0x57` with `0xA888` checks the bus.
- **External flash**: Kioxia SPI NAND, JEDEC ID `98 ED 51`, 4 kB pages.
  - It is bit-banged, not on a hardware SPI: CS PC5, CLK PE15 (mode 0),
    MOSI PB10, MISO PB8.
  - Read: `13 <page:24>`, then poll `0F C0` until bit 0 clears, then
    `03 <col:16> <dummy>`. Stock uses `3B`, dual output with PB10 as the
    second line.
  - Address = page << 12 | column. Status bit 5 (0x20) is set on every
    page read; the data is still good (primary and backup copies match).
  - SPI2 (PB12-PB15) is the SCT3288 interface, not the flash.

## 4. WX4818N registers

| Reg | Use | Source / check |
|---|---|---|
| `0x02` | bits 9:0 RSSI, raw | `0x08049E3C`; bench-verified, 3.95 counts/dB, 310 = -60 dBm, floor about 176 |
| `0x0E` | bit 14: carrier flag (RSSI above `0x49`) | `0x080CF3F4`; not used yet |
| `0x10/0x11` | squelch-related, stock `0xA380 \| table`, `0x041A` | `0x08115C34`; writing it does not mute audio |
| `0x12/0x13` | `0x20D8`/`0x00EE` at stock init, likely AGC/LNA gain | stock init; lowers RSSI by about 29 dB, kept as stock |
| `0x14`, `0x16` | tone generator: Hz x 10.485, `0x8000` enable | stock beep; does not produce a tone alone (7) |
| `0x30` | `0x5F00 \| level` AF level; stock fixed `0x74`/`0x77` | `0x080E4154` |
| `0x49` | squelch threshold for the `0x0E` flag | `0x080E36F4` |
| `0x52` | GPIO: `pin << 12 \| 0x40 \| level` | `0x08010B2C` |
| `0x53` | indexed gain table; entries 4 and 2 are `0x463D`/`0x2637` from 300 MHz, `0x467F`/`0x263A` below | `0x081155AC` |
| `0x56` | mode: 0 standby, 1 RX, 2 TX, 6 seen on band B | `0x08010C1C`, `0x0811B308`, `0x080D659C` |
| `0x58` | `0xF1F0` before a mode change ("apply") | `0x080D655C` |
| `0x5B` | `0x0085`, or `0x2085` in a spur-avoidance mode | `0x080E2EDC`; not written yet |
| `0x5C` | GPON DAC: `0x1000 \| value << 4`, VHF filter tuning; needs the apply sequence | `0x0801A35C` |
| `0x5E/0x5F/0x60`, `0x2B` | band constants, switched at 390 MHz | `0x080E3C68` |
| `0x66`, `0x0B-0x0E`, `0x54/0x55` | frequency counter (not tuning) | `0x080D5A50`, `0x080D5BD4` |
| `0x68/0x69` | **RX frequency in Hz**, low/high word | `0x08116674`; bench-verified |

- A new frequency takes effect only on a **standby -> RX** transition
  (`0x56` 0 then 1). The driver runs the full stock RX entry
  (`0x08010C1C`) on every retune.
- **Front-end GPIOs for band A** (`0x0810D490`, band A branch), bench-verified
  with an RSSI scan:
  - UHF (from 300 MHz): GPIO0=1, GPIO6=0, GPIO7=0, GPIO4=1.
  - VHF: the reverse.
  - The opposite GPIO7/GPIO4 polarity in that function is the band B
    branch. Using it costs about 30 dB.
  - On VHF, **band B chip GPIO5 = 1** gives +7 dB. This was found by
    measurement; stock sets it from settings-dependent code (see 8).
- OpenRTX calls `radio_updateConfiguration()` after any setting change,
  including entering the menu. Retune only when the frequency changes,
  with the speaker amplifier off, or the speaker knocks.

## 5. Factory calibration

The stock firmware keeps it in the NAND at **0x4F00000**, with a backup
copy at 0x4F40000 (`0x0813A0F0`). There are two 42-byte blocks:
block A at +0, loaded to RAM 0x200152AC, and block B at +0x100,
loaded to 0x200152D6. Values below are from the development radio.

| Field | Value | Meaning |
|---|---|---|
| A+0x00 | 2023 | crystal correction: offset = abs(cal - 2000) x f / 46014476 (f in 10 Hz units), added if cal >= 2000 (`0x08047E10`) |
| A+0x10 | 195 | squelch base raw RSSI, band A chip, UHF |
| A+0x12 | 176 | squelch base, band B chip, UHF |
| A+0x14/0x16 | 50/50 | carrier-detect thresholds, minus 14 (`0x080CF3F4`); purpose unclear |
| A+0x18/0x1A/0x1C | 10/175/253 | UHF filter tuning at 400/460/520 MHz (`0x08049FC4`) |
| B+0x16/0x18 | 188/188 | squelch base, VHF, chips A/B |
| B+0x1E/0x20/0x22 | 25/160/245 | VHF filter tuning at 136/174/180 MHz (`0x0804A0A2`) |

- Squelch threshold for `0x49` = base + step [0, 7, 13, 19, 23] (table
  at `0x0814AE05`, levels 1-5), corrected per frequency by flatness tables
  in RAM at 0x20001D02 (UHF, 5 MHz steps) and 0x20001D1B (VHF, 2 MHz
  steps). Their flash source is not found yet (`0x080E373C`).
- The driver reads both blocks at start-up and falls back to the backup
  copy, then to mid-scale defaults. It interpolates the filter tuning
  linearly between the points.

## 6. Reverse-engineering method

Tools are in `re-tools/` (Python, `capstone` and `unicorn`). The firmware
image loads at **0x0800C000**.

- `findex.py` builds a function index (`funcs.pkl`), used by the other scripts.
- `tdis.py` disassembles; `blcallers.py` lists call sites.
- `fieldrefs.py` finds which functions use which fields of a RAM struct.
- `trace.py` emulates a stock routine and logs WX4818N register writes and
  reads, GPIO writes, and GPIO configurations. Stubs:
  - write `0x080E2F64(reg, value, chip)`, read `0x080E2CAC(reg, chip)`;
  - GPIO write `0x0804FBD8(port, mask, level)`;
  - GPIO init `0x0804F7D0(port, init)`; delay `0x0811EC64`.

Pitfalls met so far:

- **Plant only specific RAM variables.** Filling all of RAM produced fake
  frequency-dependent registers. Frequency variables are in 10 Hz units:
  0x2000432C (current), 0x2000C240 (band A), 0x2000C244 (band B).
  Settings live at 0x200148E4; with zeroed RAM, emulation follows the
  wrong settings-dependent branches.
- **Stock GPIO setters write only on level changes** (bitmask shadows at
  0x2000D75E for chip A and 0x2000D760 for chip B), so traces miss pins
  that were already at the required level.
- **Some routines take the frequency as an argument**, not from RAM
  (`0x081155AC`).
- `0x080D5BD4` looks like tuning but is the frequency counter.
- **Check every register guess on the bench before building on it.**

## 7. Open items, in priority order

1. **Band B chip role on VHF.**
   - Stock puts band B into mode 6 and sets its GPIO5 depending on
     settings (`0x080C6DC0`, `0x080D659C`, `0x08111A6C`).
   - Plan: find where the settings block 0x200148E4 is loaded from flash,
     dump it from the radio, then emulate the channel-change path with real
     settings and copy it.
2. **Audio output.**
   - Beeps: the tone register writes alone send received noise to the
     speaker. The stock beep likely also switches U202/TONE.
   - The PB9 PWM path is also the future M17 speech output.
3. **Volume** through the SCT3288 (D-VOL) path, as stock does.
4. **RSSI**: decide whether to use the chip squelch (`0x49` threshold and
   `0x0E` flag) with stock thresholds, and find the flatness tables.
5. **PC0 demodulator check**: does the FM discriminator reach the ADC on
   PC0 ("TONE DET A/D")? This decides the M17 receive approach.
6. **TX**: only into a dummy load. Stock TX entry is `0x0811B308`.
7. Licensing: add `REUSE.toml` entries for the copied CMSIS files.
