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
| Backlight brightness (TIM3 PWM, stock levels) | Working |
| Settings and VFO persistence (internal flash) | Working |
| Power-off with save (PC3 hold, PA0 detect) | Working |
| USB CDC serial port (tinyusb) | Working, enumerates as 0483:4001 |
| Debug monitor over USB | Working, see 3.2 |
| Reboot into update mode from USB | Working, see 3.3 |
| Battery voltage (ADC) | Working, calibrated |
| FM receive, UHF and VHF | Working, factory calibration applied |
| RSSI / S-meter, squelch | Working, calibrated with a generator |
| Volume | Working: band A audio routed as stock, knob drives the I2C volume device (3.1), 128 steps, silent at the bottom |
| Beeps | Disabled (see 7) |
| FM transmit | Implemented from stock (power, bandwidth, CTCSS encode); keyed on air at Low: carrier, APC and supplies as stock. Mic goes straight to MICIN (not stock, deviation unverified) until the SCT3288 path works (3.5). Off unless `CONFIG_D890UV_TX` (`hwconfig.h`) |
| CTCSS decode | Not done: stock decodes in software from PC0 (4.2) |
| LEDs | Working: PE4 analog RX (green), PE5 TX (red) |
| M17 / digital | Not started |

## 2. Building and flashing

- Build: `meson compile -C build_cm4 openrtx_d890uv_wrap`. This makes
  `openrtx_d890uv.spi/.CDI/.CDD` with `scripts/anytone_wrap.py`. The wrap
  output is byte-identical in format to the stock update files.
- If meson hangs cloning `subprojects/XPowersLib`, clone it by hand and add
  the `packagefiles` contents.
- Flasher: <https://github.com/sp5wwp/anytone-flasher>:
  `python3 flash_anytone.py build_cm4/openrtx_d890uv.spi /dev/ttyACM0 --yes`.
- With OpenRTX running, no keys are needed (see 3.3):
  `FLASHER=.../flash_anytone.py re-tools/reflash.sh build_cm4/openrtx_d890uv.spi`.
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
- **Power**: PC3 power hold, PA0 power-on detect, PA15 TX power.
  - Stock polls PA0 in its main loop (`0x080D6954`): PA0 **high** for
    10 ms means the switch is off. It then shuts the chips down and clears
    PC3, which cuts the supply. The I/O table says "H=ON", which disagrees.
    A tick watchdog (`0x08057DB0`) also clears PC3 if PA0 is high and
    the main loop has not polled for 1000 ticks.
  - OpenRTX drives PC3 high at start-up and takes the PA0 level read then
    as the "on" level, so either polarity works. On power-off it saves the
    settings, then clears PC3.
- **Backlight**: PB0 is TIM3 channel 3 (AF2). Stock (`0x08062E40`) uses
  PWM mode 1, prescaler 3, period 4095. Its 5 brightness levels (settings
  byte `0x26`, table at `0x0814A90A`) are 2550, 2750, 2850, 2950 and 4095.
  The backlight timeout writes 0. Duty setter: `0x0801A3B4`.
- **Internal flash used by stock**:
  - 0x0800A000, the last bootloader page: 16 bytes written by `0x080C500C`.
    Never touch it.
  - 0x081E4000-0x081EFFFF: data copied from the NAND (`0x08058614`).
  - OpenRTX keeps settings and the VFO in 0x081FC000-0x081FFFFF, which
    stock does not use (`nvmem_D890UV.c`). The linker script excludes it.
- **Audio**:
  - PC12 is the LN4898 speaker amplifier, **high = on**. The I/O table's
    "SPK MUTE H=OFF" means the mute is off when high.
  - PD0-PD2 are audio routing switches; for RX use PD2=1, PD1=0, PD0=0.
    PE0 is mic mute (high), PA1 Bluetooth power, PE1 APC power.
  - Band A receive audio reaches the amplifier only with band B chip GPO2
    high; the level is set by a volume device on a bit-banged I2C bus
    (PB6 SDA, PB7 SCL). See 3.1.
  - Full routing is in 3.1.
- **DAC1**:
  - Channel 2 (PA5) is the **UHF front-end filter tuning voltage**.
    The I/O table calls it "RXVCA"; it is not the volume.
  - Channel 1 (PA4) is TX APC.
- **RF**: two WX4818N transceivers (BK4819-like, no datasheet).
  - Band A CS is PB5; band B CS is PD3. SCLK PB3 and SDIO PB4
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

### 3.1 Audio and baseband routing

Source: schematic excerpts in `materials/D890UV-M17 BASE.doc` (the
manufacturer's proposal for M17), and the digital board schematic
`materials/DIGI-CODEC.pdf`. Not yet bench-verified. The schematic labels
the transceivers "QA4818P"; they are the WX4818N chips. All routing
switches are RS2057 (SPDT: S low = B0, S high = B1).

- **Speaker amplifier input, net AF-REP.** It is a summing node:
  - band A AFOUT or FM-A (broadcast receiver audio), through a switch on
    band B chip GPO2 (B1 = AFOUT);
  - band B AFOUT, through a switch on PC7 (I/O table: "B AFOUT MUTE,
    L = mute");
  - AFIO (to and from the digital board) through 4.7 k;
  - the switch on band B chip GPO1: B1 = **D-VOL** (digital board codec
    output), B0 = **TONE** (PB9 PWM through a 2nd-order RC filter,
    4.7 k / 2.2 nF then 33 k / 220 pF).
- **PC0 ("TONE DET A/D")**: a switch on band B chip GPO0 selects band A
  AFOUT (B0) or band B AFOUT (B1). The signal is AC-coupled to an op-amp
  stage (gain about 3.7, 270 k / 100 k) and goes to PC0. BEEP/AF from the
  digital board is summed at the same op-amp input.
- **TX modulation path**: AF-REP (10 k) → switch on PD0 (B1, so PD0
  high passes it) → switch on PD1 (B1; its B0 is the **VCTCXO** net) →
  switch on PD2 (B0; B1 is MIC) → band A chip MICIN. The VCTCXO net may
  allow modulating the reference too (two-point modulation); unverified.
- **Microphone**: MIC goes to band B chip MICIN as well. In the proposal,
  band B chip acts as the microphone preamplifier: MICIN → its AFOUT
  (B-AFOUT) → PC0.
- **Digital board**: SCT3258/3288 baseband plus a TLV320AIC3204 codec,
  12.288 MHz clock. Its outputs to the main board are D-VOL and BEEP/AF;
  AFIO and MICO/MICI go through the board connector.
- **Band A chip GPIO** (I/O table, names cut off in the excerpt):
  GPON "D/A OUT", GPIO0 "UHF RX P..", GPIO1 "DIGI AF_..", GPIO3 "TX CONT..",
  GPIO4 "RX-B U/V..", GPIO5 "VHF RX ..", GPIO6 "VHF RX ..",
  GPIO7 "RX-A U/V..".
- **Band B chip GPIO**: GPIO0 "A/B TONE.." (PC0 source), GPIO1 "DIGI AF C.."
  (speaker source), GPIO2 "A AFOUT.." (AFOUT or FM-A), GPIO3 "VIBRATE",
  GPIO4 "AM/FM A..", GPIO5 "AM/FM PO..", GPIO6 "GPS POWER",
  GPIO7 "AM/FM V..". GPIO7 also drives a transistor on the band B
  RF output.
- **Receive audio in stock (V1.05)**, checked by ear on the radio:
  - The routing function `0x08118DF8` takes a set of bits: bit 0 drives
    band B chip GPO1 (D-VOL or TONE), bit 2 band B chip GPO2 (band A
    AFOUT to AF-REP), bit 3 PC7 (band B AFOUT). The GPIOs go through
    `0x08019AB4`, which writes WX4818N register `0x52`.
  - With GPO2 low the band A audio only leaks to the speaker, far below
    stock even at full volume. With it high the level matches stock.
  - The level is set by a device at 7-bit I2C address `0x2E`: two bytes,
    register `0x00` and a value 0-127 (`0x0806536C`; the address is
    retried up to 300 times). It is linear: value 1 is still clearly
    audible, value 0 leaves a faint leak. Stock reads the knob in 16 steps
    and maps them through a table at `0x0815153E` (16 rows, row chosen by
    a setting); its first step also jumps from 0 to 1.
  - Writing the device knocks in the speaker if done continuously; stock
    and the port write only when the value changes.
  - WX4818N register `0x48` (stock `0x082E` narrow, `0x9826` wide) does not
    change the speaker level, even at `0x0000`.
  - A 1 kHz SCT3288 line-out tone (`ANALOG_TONE`, output 0) is not heard
    on the speaker, so AFIO does not carry it there.
  - The port sets GPO2 in RX and clears it for TX, maps the knob with a
    square law onto 0-127, and below knob level 3 clears GPO2 and keeps
    the amplifier off for true silence.
- **PB9 PWM (TONE)**: TIM4 channel 4, 8-bit PWM. The TIM5 interrupt
  (`0x08120E70`) updates it from a sample buffer (`0x200087BC`) or a sine
  table (`0x081505C0`): beeps, tones and voice prompts, not the receive
  audio.
- **Manufacturer's M17 proposal**: RX is band A AFOUT → PC0 ADC →
  demodulation and Codec2 in the MCU → PB9 PWM → TONE → speaker. TX is
  mic → band B chip → PC0 → 4FSK synthesis → PB9 PWM → TONE → AF-REP →
  band A MICIN. The digital board is not used.

### 3.2 USB and the debug monitor

- PA11/PA12 go straight to the MCU OTG_FS controller. Stock runs a CDC
  port in normal operation ("STM32 Virtual ComPort"); its setup:
  - `0x08051058`: 48 MHz clock from HSI48, PA11/PA12 AF10 without pulls,
    OTG clock on, `PWR->SVMCR` USV set, interrupt 73;
  - `0x08120BD8`: CRS synced to the USB SOF, reload 47999, error limit 34,
    trim 32;
  - `0x0812A280`: full speed, embedded PHY, no VBUS sensing, no DMA.
- OpenRTX uses tinyusb 0.20.0 (the old pinned revision had no STM32U5
  support), `platform/drivers/USB/usb_D890UV.c`. A thread runs the stack
  and feeds command lines to the debug monitor (`monitor.c`).
- Monitor commands (hex numbers; each reply ends with `OK` or `ERR ...`):
  `ping`, `rb addr len`, `rw addr [count]`, `wb addr val`, `ww addr val`,
  `nr addr len` (NAND, addr = page << 12 | column), `xr a|b reg`,
  `xw a|b reg val`, `xg a|b pin 0|1`, `update`. SCT3288: `si` (power-up
  and boot status), `so` (power off), `sw hex` (send), `sr` (read),
  `sa 0|1` and `sl` (background read and frame log), `sx hex [timeout]`
  (send a packet of up to 1536 bytes and print the answer, `RX ...`).
  Memory accesses are not checked. Command lines may be 3 KB long. Host
  client: `re-tools/monitor.py` (command line or module).

### 3.3 Bootloader and update hand-off

Read from the radio with the monitor (`rb 08000000 C000`); Keil start-up,
`main` at `0x08006B10`.

- Update mode is decided by `0x08000640` alone: PA8 (top key) and PA10
  (PTT) read low, twice each. No RAM, backup-register or flash flag.
- Otherwise it jumps to 0x0800C000 through `0x08006A7C`, which only loads
  the MSP and branches: clocks, RAM, VTOR and SysTick stay as the
  bootloader set them.
- `update` (monitor) writes a flag at 0x200BFFF8 (top of SRAM3, kept across
  reset since `FLASH_OPTR` SRAM_RST = 1) and resets. The bootloader starts
  OpenRTX; `early_boot_hook()` (`bootloader.c`), first thing in
  `Reset_Handler`, sees the flag and branches to the update branch
  `0x08006B32` with MSP 0x20003818. The bootloader code at 0x08006B28 is
  checked first; another version is left alone.
- 0x0800A000 (last bootloader page) holds a 16-byte ID record. The stock
  application's serial commands `G` and `T` read and write it
  (`0x080C48AC`).

### 3.4 Stock data in the NAND

- Settings block (RAM 0x200148E4, 0x160 bytes, ends with `0x55` at
  +0x15C): NAND 0x3500000 or 0x3540000, chosen by `0x08023EA8`; loaded at
  `0x080DDEE0`, which also loads other configuration blocks. Byte 0x26 is
  the backlight level (0-4).

### 3.5 Digital board (SCT3288) and the analog FM audio path

Stock FM TX audio goes through the digital board: PE0 high selects MICO
(the board's output) and PD2 low feeds MICIN from the switch chain. With the
board unpowered this gives a carrier without modulation (checked on air);
PE0 low + PD2 high (radio mic straight to MICIN) modulates but is not stock
and its deviation is unverified. Sicomm's application note confirms the
SCT3258/3288 handles analog FM: mic and modulation through the TLV320AIC3204
codec, TX gain, limiter, CTCSS/DCS, two-point modulation (VCO + VCTCXO).

- Power-up `0x080D6F5C`: PD8 = 0, PC6 = 1 (board power), SPI2 init
  (`0x0806207C`: master, 8 bit, mode 0, software NSS), PB12 = 1 (chip
  select), PD10 = 0, PD9 = 0, then PD8 (reset) 1, 0, 1 with 100 ms steps.
  Power-off `0x080D7964`: PC6, PD8, PD9, PD10 low.
- Byte layer: write `0x0810387C(byte)`, read `0x08103838()`, both
  HAL SPI transmit-receive with a 5000 ms timeout. `0x0810A674(buf, len)`
  waits for the chip (`0x0810A6DC`), lowers PB12, sends, raises PB12.
  `0x0802E8F8(buf, len)` is the library's send hook.
- Packets (Sicomm "Packet Interface" note): start bytes `84 A9 61`, 2-byte
  length (MSB first, excluding the 6-byte header), TYPE (bit 7 vocoder
  bypass, bit 6 codec bypass, bit 5 read, bit 4 far end, bits 3:0 type:
  0 control, 3 DPMR, 4 program, 5 DMR), fields (1-byte identifier + data),
  optional parity field `2F xx` (XOR of everything after the start bytes).
  Stock builds headers in `0x0802DE50` and `0x0813AF78`.
- The library queues commands and sends them from `0x0802DFF8`.
  `0x0810C3B0(kind, timeout)` (61 call sites, kinds 3/4/5, timeouts 200 or
  2000 ms) sends a prepared packet and waits; each caller is a field setter.
  Library state at 0x20004620: zero until the library is opened, so
  emulating a setter alone sends nothing.
- Pins (stock GPIO setup `0x0806235C`, `0x08061770`, `0x080617C4`): SPI2 on
  PB13/PB14/PB15 (AF5, no pull), chip select PB12, power PC6, reset PD8,
  PD9/PD10 outputs held low, PD11 input = "data ready" (the library reads
  bytes while it is high, `0x0810A6DC`), PD4-PD7 inputs. None is used by the
  port.
- Library open `0x080F403C(mode)`, emulated with `re-tools/sctemu.py` (fake
  chip on the byte layer): reset (PD10, PD9, PD8 low, 50 ms, PD8 high,
  100 ms); the chip must report ACK_MESSAGE (field 0x17) = 0x0E; the host
  sends a raw interface word without packet header (`0x08100664`):
  `74 A9 01 00 01 00` in mode 0 (`74 A9 01 03 90 00` in mode 1), answered
  by ACK_MESSAGE 0 or 1; then CODEC_SELECT (field 0x2D) = 4. Stock opens in
  mode 1, then mode 0, after powering the board.
- Emulating single library functions sent nothing at RX entry or PTT; that
  was an emulation artefact (empty library state). The whole-firmware
  emulation (3.6) shows the host sends the full analog set-up and per-PTT
  packets.
- **On the radio** (hardware SPI2, `spi_stm32u5.c`; software SPI read the
  application one bit off and was ignored by it): boot status `17 0E`;
  interface word -> `17 01` (ready) after 2.75 s; CODEC_SELECT 4 -> `2D 00`;
  codec writes `0B 02`, `0B 82` (TYPE 0x40, codec bypass) -> `17 00`;
  `79 41 40 42 44 08` -> `79 00`; Q_SW_VER -> "V3.01.01A3". The chip must
  be read continuously (frame line PD7 + byte ready PD11, `0x0810A6DC`), as
  the stock main loop does, or it repeats `17 01` and the audio taps at about
  5 Hz. Power-off must leave the control lines floating, or it does not
  boot again.
- Field 0x1A PROCESS_MODE = 0x80 is analog mode (Sicomm note). Stock sends it
  from `0x080F36F0` (with codec `08 00` and field `6F 01`), called from the
  library report handling (`0x080F40F8` command 0 -> `0x080FBC44`), which the
  application polling loop `0x080DFC0C` runs. Sent alone on the radio: reply
  `17 11`, then repeated `17 01` (chip restarting) and no TX audio.
- Stock FM TX on the main band (0x2000F988 = settings 0x2C) uses PD2 = 0:
  the audio does come from the digital board.
- `0x080F40F8(cmd, arg0, arg1, buf, len)` is the library API of the Qixiang
  note (SCT_Module_Read/Write/Ioctrol), 19 commands. Setter map (field sent
  by each library function) is in `re-tools/sctemu.py` runs; analog ones:
  0x3C ANALOG_CONFIG (`0x080E5E98`), 0x3D CTC_PARAM (`0x080E7EA4`), 0x39
  DC_OFFSET (`0x080E88F4`), 0x6B MISC_GAIN (`0x080F369C`), 0x70 EQUALIZER
  (`0x080F0808`), 0x1A PROCESS_MODE (`0x080F8560`), 0x18 WORK_MODE
  (`0x081003D0`), codec bypass writes (`0x080F2540`, `0x080F2594`...).
- The library state (0x2000F8E4 call state, 0x2001257C configuration) is
  filled by the stock start-up from settings, channels and calibration.

### 3.6 Whole-firmware emulation (`re-tools/fullemu.py`)

Runs stock V1.05 from the scatter-load through `main` and the uC/OS-II
tasks, with the NAND read live from the radio (cached), a fake SCT3288 and
PTT simulated on PA10. Stock is **not bare metal**: `main` starts uC/OS-II
(14 tasks, PendSV `0x0800C2C8`, OSTCBCur 0x2000A47C, OSTCBHighRdy
0x2000A484, OSRdyGrp/Tbl 0x2000A470/71, critical sections on BASEPRI
`0x0800C274`/`0x0800C280`). Things the emulation must provide:

- RTC backup register 0 = 0x5050 (clock set; 0x6060 shows the set-clock
  screen and stops start-up, anything else initialises the RTC).
- The bootloader image and MCU unique ID: `0x080215C0` checks the record at
  0x0800A000 against the ID and stops in a loop if they differ.
- NAND ID `98 ED 51` (`0x080C9A48`).
- An acknowledging bit-banged I2C on PB6 (SDA) / PB7 (SCL).
- `0x0811EC64` delays through OSTimeDly (5 ms ticks) once uC/OS runs: it
  must not be stubbed then, or tasks never block.
- A battery sample at 0x20001D36 (ADC DMA buffer): at or below 6.00 V
  (`0x08039024`) stock shows screen 0x0B and ignores PTT.
- TIM5/TIM6/TIM7 update interrupts and SysTick.

Key path: key scan `0x0811D9CC` (PTT = PA10 low) -> code 0x18/0x19 in
0x20007CC0 -> queue 0x2001EFB4 -> UI task (prio 12) -> dispatcher
`0x08068FC8` (screen state 0x20004B98) -> TX queue 0x2001EFC4 -> task prio 8
-> PTT handler `0x0811B308`.

Captured SCT3288 sequence (host packets; replies of the fake chip follow the
real chip where known; `1A 80` gets `17 11` as on the radio):

- Start-up, after boot status `17 0E` and interface word `74 A9 01 00 01 00`:
  `2D 04`; codec (TYPE 0x40) `0B 02`, `0B 82`; TYPE 5 `79 41 40 42 44 08`;
  read `33`; `10 04`, `10 10`; six EQUALIZER `70` packets (`21/22/23`,
  `01/02/03` + 10 coefficient bytes); TYPE 5 `2F 08`, `1D FF`,
  `41 00 2F 00 00 00`, `6C 00 00 00 3C 0E 10 0A 28 FF FF FF FF`, `1C 00 00`;
  `69 1F 40`, `68 0B B8`; codec `08 00`, `09 00`, `11 00`, `08 00`, `0F 90`,
  `10 1E`; TYPE 5 `7A 00 87 02 D0 02 D0 02 D0`; `06 FF FF FF FF FF FF FF`;
  `6B 08 00 08 00 61 A8 80 00 40 00 00 00`; TYPE 5 `19 00`, `18 00 00 00`,
  `1A 80`, `26 04 00`; `3D 03 84 02 58 01 01 02 01 07 06 80 D0 05 00 02 26 00
  76 03 B3 50 00`; `39 08 00 08 00 08 00 01 F4 08 00 F2 30`;
  `3C CF 46 50 05 78 00 00 00 00 00 BE`; then again `06 ...`, `6B ...`,
  TYPE 5 `2C 0D AC`; `73 62 25 01 A8`; TYPE 5 `18 00 00 00`, `26 04 00`;
  read `33`, read `32` (version) twice.
- RX (entering the main screen): `39 ... F2 30`; TYPE 5 `18 01 00 00`; codec
  `08 00`; TYPE 5 `19 02`; codec `08 1F`; TYPE 5 `19 00`, `18 00 00 00`,
  `26 04 00`.
- PTT press: codec `10 1E`; TYPE 5 `19 00`, `18 00 00 00`;
  `39 08 00 08 00 08 00 01 F4 08 00 10 00` (last two bytes differ from RX);
  TYPE 5 `18 00 00 00`, `26 04 00`; GPIOs PA1 = 0, PE0 = 1, PD2 = 0,
  PD1 = 0, PD0 = 0, PE1 = 1, PA15 = 1; WX4818N TX set-up (as 4.1); then
  TYPE 5 `26 00 00`, `18 02 00 00`, codec `10 1E`, TYPE 5 `78`.
- PTT release: TYPE 5 `21`; PE0 = 1, PD2 = 1; then the RX sequence again.

The full logs (with WX4818N writes) come from
`fullemu.py <fw> --boot <bootloader> --uid <uid> --ptt 1000:1800 --quiet-wx`.

**On the radio** (sequence sent by a test build of the port, every reply
logged):

- The real chip accepts the whole sequence (code 0 to every packet,
  including `1A 80` and the call start `78`), and reports nothing while
  transmitting. Answers differing from the fake chip: read `33` returns
  `33 00 00 21 00 06 78 05 00 00 00`; `10 04` may come with `17 01`
  (ACK_MESSAGE 1, "response to system reset"); `19 02` in the RX entry
  adds NEW_EVENT reports `30 1B 0C` (carrier detected) and `30 1B 10`.
- With the speaker amplifier (PC12) on, the chip restarted while its codec
  and vocoder started (silent afterwards, even to the interface word). Stock
  keeps PC12 low during the set-up; with it low the set-up always passed.
- WX4818N register `0x30`: its low byte (the AF level in the port) also
  gates the TX audio input. With it 0 (volume at minimum) there is no
  modulation at all; stock has `0x5F77`.
- Stock TX with these packets, PE0 = 1, PD2 = 0 and `0x30 = 0x5F77`:
  carrier with strong 24 kHz sidebands, no speech. With WORK_MODE codec
  loop back (`18 02 01 00`) the microphone gets through (breath noise,
  low level), so microphone -> codec -> board output -> MICIN works; the
  SCT3288 analog processing does not pass speech yet. The 24 kHz component
  is there in both modes.
- WORK_MODE debug byte 1 (1 kHz sine on the modulation port) gives a
  plain carrier in analog mode (the faint tone seen first leaked into the
  internal microphone). The MOD port is wired to the modulator, though:
  ANALOG_TONE (field `0x1F`) with TONE_CTRL bit 7 set (remote, MOD port)
  is heard on air; with bit 7 clear (line out) it is not. The port sends
  the 1750 Hz tone burst this way (`sct3288_setTone()`).
- ANALOG_TONE: TONE_CTRL (bit 7 MOD port, bits 6:0 repeat count, 0 stops,
  127 endless), then four frequencies in Hz (two per tone), four
  amplitudes (8 bit, 6 fractional), two lengths in ms; control packet,
  answered `1F 00`.
- Stock opens the chip twice: the whole sequence, a board power cycle
  (PC6 low, power-up, reset), the whole sequence again. Seen in the
  emulation once the fake chip answers the vocoder switch with `17 01` as
  the real one can. With a single pass the codec output carries 24 kHz
  sidebands and no audio; with two passes the 24 kHz is gone and the
  microphone modulates (breath noise), in normal TX mode.
- Full key-up comparison with chip A and B snapshots taken by a test build
  (after the `0x19` and `0x30` fixes): SCT3288 packets, routing GPIOs and
  chip A registers match stock (apart from readback bits, `2E` and the
  frequency). Stock also sets chip B up at key-up, tuned to the TX
  frequency with microphone gain `48 = 9826` (from `0x080E2453` on);
  replaying those writes changed nothing on air, so chip B is not in the
  TX audio path.
- With two passes the port behaves like stock on air (stock V1.05 flashed
  back for the comparison, VFO 435.000 MHz analog, same weak speech level),
  so the remaining level question is the radio's microphone path, to be
  inspected in the hardware.
- In FM the microphone does not go through the MCU: ADC1 scans channels 1,
  2, 3, 12 (PA7, microphone amplifier) and 19 into 0x20001D34 by circular
  DMA (GPDMA1 channel 10), but no stock code reads the microphone slot.
- The I2C bus on PB6/PB7 holds a charger (0x6A), an accelerometer
  (0x19, WHO_AM_I 0x0F) and a third read-only device: no audio parts.

### 3.7 SCT3288 firmware update (`re-tools/sctflash.py`)

The chip came with firmware V3.01.01A3; AnyTone's change logs for V1.03
to V1.05 list V3_01_01A6 (`SCT3288QD_UpdateFlash_V3_01_01A6_20250610.hex`
in the V1.05 package). It was updated to A6 from Linux with `sctflash.py`,
a reimplementation of the "Flash Update" of Sicomm's SCT_PORT tool
(decompiled: `SCT_Device.dll`, `SCT3252::DownLoadCustomApplicationHexFileToFlash`),
going through OpenRTX's own SPI link instead of AnyTone's bridge.

- AnyTone's procedure: power on stock holding PF3 + # (code 0x0C, PA8 low,
  checked late in start-up: hold for a few seconds) -> "BOOT MODE FOR SCT".
  Stock (`0x0801F800`, bridge `0x0801A83C`) opens the chip in mode 1 and
  passes USB data to it. Read-only queries through that bridge got no
  answer here; the update was done without it.
- Programming mode: interface word `74 A9 01 03 90 00` (mode 1), answered
  `17 01`. The chip then runs its loader (Q_SW_VER `V3.01.01P2`,
  Q_PRODID `SCT3288QD`, Q_FID `20 03 80 E3 00`).
- Packets as usual (`84 A9 61`, length, type, fields). PARITY_ENABLE
  (`16 01`) adds field `2F xx` to every packet both ways, xx = XOR of the
  bytes from the length MSB to the `2F`.
- `InitFlash(start, region)`: `16 01`, 50 ms, control packet `93 xx`,
  xx = start | region << 1, answer `93 00` (5 s). `WritFlash`: type 3,
  `94 A2 A1 A0 L data`, L = length capped at 255, answer `94 00`. Two
  attempts, resend on ACK_MESSAGE 3 or 6. End: `93 00`, then `16 00`.
- The HEX file: sections separated by empty type-4 records whose address
  field is the 64 KB page; data are written contiguously from the address
  of the record that triggers a region. Regions for "SCT3288" (version
  type 2, region code): User 0x100 (1), Vocoder 1 0x22000 (5), Vocoder 3
  0x63000 (3), Vocoder 4 0x35000 (10), Vocoder 5 0x77000 (15), Vocoder 6
  0x53000 (12), Vocoder 7 0x56000 (13); System 0x39000 (2) is not in the
  A6 file, so the loader is not rewritten.
- Usage (OpenRTX with `sx` on the radio; the script disables the radio
  driver's use of the chip, power-cycle the radio afterwards):
  `sctflash.py <hex> --dry-run` (print the packet stream), `--rehearse`
  (programming mode and parity, stops before the first erase), `--run`.
  The A6 update took 24 s: 7 erases, 272 writes, no retries.
- The firmware version read at start-up is shown in Info -> "SCT FW".

## 4. WX4818N registers

| Reg | Use | Source / check |
|---|---|---|
| `0x02` | bits 9:0 RSSI, raw | `0x08049E3C`; bench-verified, 3.95 counts/dB, 310 = -60 dBm, floor about 176 |
| `0x0E` | bit 14: carrier flag (RSSI above `0x49`) | `0x080CF3F4`; not used yet |
| `0x10/0x11` | squelch-related, stock `0xA380 \| table`, `0x041A` | `0x08115C34`; writing it does not mute audio |
| `0x12/0x13` | `0x20D8`/`0x00EE` at stock init, likely AGC/LNA gain | stock init; lowers RSSI by about 29 dB, kept as stock |
| `0x14`, `0x15`, `0x16` | tone generator: Hz x 10.485 (`0x14` tone 1, `0x15` tone 2), `0x16` bit 15/bit 7 enable | stock beeps and DTMF side tones (`0x081162B8`, `0x08116378`, table `0x0814A93E`); not heard on air while transmitting |
| `0x30` | `0x5F00 \| bits`: the low byte enables parts of the audio chain, not a level. `0x77` RX and TX with the microphone, `0x64` while the tone generator plays (`0x0811C83C`), `0x00` mutes everything including TX audio | `0x080E4154`, `0x0811C83C`; checked on air |
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

### 4.1 FM transmission (stock analog TX path)

From the PTT handler `0x0811B308`, analog branch `0x0811BE8E-0x0811C0D0`,
emulated with the settings and calibration of the development radio. Our
sequence (`wx4818n_startFmTx()`) gives the same register writes as stock for
UHF/VHF, narrow/wide, with and without CTCSS.

- Audio routing: `0x081140EC(mode)` sets PA1, PE0, PD2, PD1, PD0. FM TX uses
  mode 0, which with the TX flag set gives PE0 = 1, PD2 = 0 (`0x081141FC`):
  the modulation comes from the digital board (3.5). The port uses PE0 = 0,
  PD2 = 1 meanwhile (radio mic straight to MICIN). Modes 3/4 (mic → PD0/PD1 chain) are used only with
  settings byte `0x143` set, probably the noise-reduction board.
- Front end in TX (`0x0810D490`): band A GPIO0 = 0, GPIO6 = 0 (receivers
  off), GPIO1 = 0. GPIO3 selects the transmit path: high from 300 MHz
  (`0x080E3D38`). Stock leaves GPIO3 as it is in RX.
- Mode setup `0x0811586C(digital, tx, chip)`: TX values are the same for
  analog and digital. Digital RX (1, 0) zeroes the audio filters `0x1C-0x23`:
  useful for M17 reception later.
- Bandwidth: channel byte +8 bits 5:4 (non-zero = wide, `0x0804BCBC`).
  It changes only the TX mic gain, register `0x48`: 0x082E narrow, 0x9826
  wide (`0x08116480`). Stock RX does not depend on bandwidth.
- Power: channel byte +8 bits 3:2 = level 0-3 (Low/Mid/High/Turbo: VHF
  0.2/2.5/5/7 W, UHF 0.2/2.5/5/6 W). APC = base[level] + table[level][MHz] -
  table[level][ref], from the calibration (5). DAC1 channel 1 (PA4) gets
  APC * 16, applied once after PA15 goes high (`0x0811A75C`, setter
  `0x0801694C`). No ramp.
- Power-up order: PE1 (APC supply), PA15 (PA supply), then the APC level.
  Power-down (RX entry `0x08010C1C`): PA15 = 0, PE1 = 0 first.
- CTCSS encode (`0x080E3D98`): value = Hz * 20.97152 (2^21 / 100000). Register
  `0x2E` is multiplexed by bits 15:13: entries 0 and 1 get the tone, entry 6
  the modulation calibration. `0x2D` = 0x0215 up to value 0xACC, 0x3215
  above; `0x2F` = 0x0A0F. Without a tone, `0x08127BE8` writes another set.
  The 51 stock tones are the table at `0x0814A820` (0.1 Hz units).
- DCS encode is `0x080E3ED0(code, inverted)`, not analysed yet.
- Channel struct (pointer 0x2001AFBC, band A 0x2000C230, band B 0x2000C234):
  byte +8 bits 1:0 mode (0 analog), 3:2 power, 5:4 bandwidth; +0x39 signed
  TX frequency offset; +0x1A high nibble digital protocol type. Tone record
  at 0x2001DFEC: +0x18 type (1 CTCSS, 2 DCS), +0x1A CTCSS index, +0x1C DCS.

### 4.2 CTCSS decoding

- Stock reads only RSSI (`0x02`), a chip ID (`0x01`) and the frequency
  counter (`0x0B-0x0E`, `0x54/0x55`) from the chips: **it does not get a tone
  detection from the WX4818N**.
- ADC1 runs a 5-channel scan with DMA into 0x20001D34 (`0x0805C4AC`): PC0
  (channel 1, demodulated audio), PC1 battery, PC2 volume, PA7 (channel 12,
  VOX), one internal channel. A timer interrupt (`0x08120EEC` → `0x080D7D70`)
  takes the PC0 sample, keeps 8 bits, and feeds groups of 4 to a software
  decoder. So CTCSS is decoded in the MCU from PC0, the same input the M17
  demodulator will use.

## 5. Factory calibration

The stock firmware keeps it in the NAND at **0x4F00000**, with a backup
copy at 0x4F40000 (`0x0813A0F0`). There are two 42-byte blocks:
block A at +0, loaded to RAM 0x200152AC, and block B at +0x100,
loaded to 0x200152D6. Values below are from the development radio.

| Field | Value | Meaning |
|---|---|---|
| A+0x02..0x05 | 47/78/95/113 | UHF APC base, power levels 0-3 (4.1) |
| B+0x02..0x05 | 46/75/96/115 | VHF APC base, power levels 0-3 |
| A+0x06 | 0x046A | UHF modulation, register `0x31` in TX |
| B+0x06 | 0x002D | VHF modulation, register `0x31` in TX |
| A+0x0C | 0x02B7 | modulation, register `0x2E` entry 6 in TX (both bands) |
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
  steps) (`0x080E373C`). They come from cal +0x1400/+0x1500 (below).
- The full layout, from the loader `0x0813A0F0` (emulated with the NAND read
  `0x08037E78` stubbed): blocks every 0x100 bytes, primary and backup
  identical on the development radio.

  | Offset | Size | RAM | Use |
  |---|---|---|---|
  | +0x000 | 42 | 0x200152AC | block A |
  | +0x100 | 44 | 0x200152D6 | block B |
  | +0x200 ... +0xB00 | 8-39 | various | not analysed |
  | +0xC00/+0xD00/+0xE00/+0xF00 | 121 each | 0x2000C178/C02C/C0D2/BF86 | UHF APC per MHz from 400 MHz, levels 0-3 |
  | +0x1000/+0x1100/+0x1200/+0x1300 | 45 each | 0x2000C1F1/C0A5/C14B/BFFF | VHF APC per MHz from 136 MHz, levels 0-3 |
  | +0x1400/+0x1500 | 25 each | 0x20001D02/1D1B | squelch flatness UHF/VHF |
  | +0x1600/+0x1700 | 25 each | 0x20003056/306F | probably the same for band B |
  | +0x1800 | 28 | 0x20015302 | not analysed |
- The driver reads both blocks at start-up and falls back to the backup
  copy, then to mid-scale defaults. It interpolates the filter tuning
  linearly between the points.

## 6. Reverse-engineering method

Tools are in `re-tools/` (Python, `capstone` and `unicorn`). The firmware
image loads at **0x0800C000**.

- `xref.py` finds code that loads a constant or address range (literal
  pool or movw/movt), e.g. peripheral bases or RAM variables.
- `findex.py` builds a function index (`funcs.pkl`), used by the other scripts.
- `tdis.py` disassembles; `blcallers.py` lists call sites.
- `fieldrefs.py` finds which functions use which fields of a RAM struct.
- `wxmap.py` lists every stock WX4818N register write and read with the
  functions doing it and the constant values: the starting point to find
  which function handles a feature.
- `sensitivity.py` runs a routine with `trace.py`, then flips each RAM byte
  it reads and reports the bytes that change its output: it finds the
  settings, channel fields and calibration a routine depends on.
- `monitor.py` and `reflash.sh` talk to the radio (3.2, 3.3).
- `sctflash.py` updates the SCT3288 firmware (3.7).
- `fullemu.py` runs the whole stock firmware (3.6). Options: `--ptt`
  (press window), `--watch` (log r0/r1 at addresses), `--watchmem` (log
  writes), `--count` (count calls), `--trace start:end` with `--funcs`
  (function or `all` block entries), `--peek`, `--dump`, `--batt`,
  `--watchread` (count reads), `--periphw` (log peripheral writes in a
  window), `--mic` (sine in the microphone ADC slot). About 15 s per
  emulated second once the NAND cache is filled (the first run reads about
  800 pages from the radio).
- `trace.py` emulates a stock routine and logs WX4818N register writes and
  reads, GPIO writes, and GPIO configurations. Options (environment): load
  RAM images (`RAM`), poke bytes (`POKE`), stop inside a function (`STOP`,
  to emulate one block of a large function), stub functions and log their
  arguments (`STUBS`), log RAM reads (`RTRACE`), log peripheral writes
  (`PERIPH`), dump memory (`DUMP`). Stubs always in place:
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
- **A sensitivity test with 0x01/0xFF can hide a field**: both values also
  change the channel mode bits of channel byte +8, which masked the
  bandwidth bits 5:4. Test single bits of a byte that holds several fields.
- **Register values written from RAM shadows need the shadow planted**:
  `0x08115DE8` writes WX4818N `0x19` from 0x2001AFC8 (0x0381 after
  start-up); unplanted, the old TX trace gave `0x0000`.
- **Check every register guess on the bench before building on it.**

## 7. Open items, in priority order

1. **FM TX audio through the SCT3288** as stock does (3.5, 3.6). The
   port matches stock on air (two open passes, stock packets, routing,
   WX4818N registers, chip B checked too); speech is weak in stock too,
   and still with the chip on A6 (3.7): only breath noise modulates, as
   low-deviation FM. Next: inspect the microphone path in the hardware
   (element, amplifier and its supply, the K-connector switch). Then check power per level and
   deviation (narrow/wide, CTCSS).
2. **Receiver noise floor**: idle RSSI about -71 dBm (raw 267) with the
   antenna, S4 without it, against raw 176 measured earlier; not changed
   by powering the digital board or the speaker amplifier off.
3. **PC0 sampling and CTCSS decode**: timer-triggered ADC with DMA on the
   STM32U5 (GPDMA), feeding OpenRTX's CTCSS detector
   (`core/ctcssDetector.hpp`). The same path is the M17 receive input.
4. **Band B chip role on VHF.**
   - Stock puts band B into mode 6 and sets its GPIO5 depending on
     settings (`0x080C6DC0`, `0x080D659C`, `0x08111A6C`). The settings
     block is now known (3.4) and can be planted in the emulator.
5. **Audio output.**
   - Beeps: the tone register writes alone send received noise to the
     speaker. The stock beep likely also switches the amplifier input switch to TONE.
   - Voice prompts must be off (Settings, level 0): the port has no MCU
     audio output yet, a prompt never ends and, at a higher priority than
     RX, keeps the receive audio muted.
   - The PB9 PWM path (3.1) is the output for beeps, prompts and the
     future M17 speech.
6. ~~Volume~~: done, stock routing and I2C volume device (3.1).
7. **RSSI**: decide whether to use the chip squelch (`0x49` threshold and
   `0x0E` flag) with stock thresholds; the flatness tables are found (5).
8. **DCS** encode (`0x080E3ED0`) and decode.
9. ~~PC0 demodulator check~~: stock samples PC0 for tone decoding (4.2).
10. ~~Licensing~~: done. `REUSE.toml` covers the STM32U5 CMSIS files; `reuse lint`
   and `scripts/clang_format.sh --check` pass for the port files.
