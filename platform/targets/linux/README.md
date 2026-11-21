<!--
SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors

SPDX-License-Identifier: GPL-3.0-or-later
-->

# Linux emulator

The emulator runs the firmware on a Linux PC or a Mac, with the screen in an
SDL window and a shell on the terminal (type `help` for its commands). It needs
the SDL2 and codec2 development packages (on macOS, `brew install sdl2
codec2`). Each variant models the screen and keypad of a group of
radios:

| Target                      | Screen                       | Keys                    |
|-----------------------------|------------------------------|-------------------------|
| `openrtx_linux`             | 160x128 colour               | arrows, 0-9             |
| `openrtx_linux_smallscreen` | 128x64 mono                  | arrows, 0-9             |
| `openrtx_linux_mduv3x0`     | 160x128 colour               | UP/DOWN, 0-9            |
| `openrtx_linux_mod17`       | 128x64 mono                  | arrows                  |
| `openrtx_linux_rt950pro`    | 240x320 colour, three tuners | arrows, 0-9, F2, F3     |

All but the Module17 one have GPS and an RTC. The RT-950 Pro variant draws the
screen in bands of 8 rows, as the radio does.

Build and run a variant from the repository root, for example the RT-950 Pro
one:

```bash
meson setup build_linux
meson compile -C build_linux openrtx_linux_rt950pro
./build_linux/openrtx_linux_rt950pro
```

On the PC keyboard, the arrows, digits, `*`, `#`, Enter and Esc are the radio
keys, `n` is F1, F2 and F3 are F2 and F3, `m` is MONI and Page Up/Down turn the
knob. On the RT-950 Pro variant, F2 selects the next tuner.

The golden-image UI tests run on these variants: see `scripts/run_e2e.py`.
