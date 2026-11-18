# UI fonts

OpenRTX's UI text is drawn from **LVGL binary fonts** (the format produced by
[`lv_font_conv`](https://github.com/lvgl/lv_font_conv)). The generated `.bin`
blobs are committed under `openrtx/fonts/` and embedded into the firmware; a
small in-tree reader decodes them at runtime. No LVGL source is vendored.

## Layout

| Piece | Where |
|---|---|
| Committed font blobs | `openrtx/fonts/ubuntu_<pt>_<bpp>.bin` |
| Regeneration script | `scripts/gen_fonts.sh` |
| Decoder (format reader + RLE) | `openrtx/src/core/fonts/lv_font_bin.{c,h}` |
| Embedding (`.incbin`) | `openrtx/src/core/fontData.S` |
| Text rendering + `SYMBOL_*` | `openrtx/src/core/graphics.c`, `core/graphics.h` |

Each `fontSize_t` (`FONT_SIZE_5PT` … `FONT_SIZE_16PT`) maps to one blob. Colour
(RGB565) targets embed the anti-aliased **4bpp** set; monochrome (BW) targets
embed the compact **1bpp** set. The choice is made in `fontData.S` from the
`CONFIG_FONT_AA` meson flag, which is set for the RGB565 targets in
`meson.build` (the pixel format itself lives in `hwconfig.h`, which the
assembler can't include).

## Regenerating the fonts

The firmware build does **not** run `lv_font_conv` — it embeds the committed
`.bin` files. Regenerate only to change the typeface, the sizes, or the icon
set, then commit the results:

```sh
scripts/gen_fonts.sh path/to/Ubuntu-R.ttf path/to/FontAwesome5-Solid+Brands+Regular.woff
```

Sources:
- **Text** — `Ubuntu-R.ttf` (Ubuntu Font Licence 1.0), e.g. the `fonts-ubuntu`
  package.
- **Icons** — `FontAwesome5-Solid+Brands+Regular.woff` (SIL OFL 1.1), from
  <https://github.com/lvgl/lvgl/tree/master/scripts/built_in_font>.

`lv_font_conv` runs via `npx`, so only Node.js is needed (no global install).

> **Gotcha:** ninja does not track `.incbin` dependencies. After regenerating
> the blobs, `touch openrtx/src/core/fontData.S` (or clean) so the embed is
> rebuilt.

## "pt" is a slot name, not a size — the ppem calibration

The `FONT_SIZE_*PT` names are historical. `lv_font_conv --size N` sizes a font
to **N pixels** (ppem), which is **not** the nominal point value. `gen_fonts.sh`
pins a ppem per slot, calibrated so each font's digit advance and cap height
match the previous Adafruit GFXfont metrics — otherwise every layout reflows:

| slot | 5PT | 6PT | 8PT | 9PT | 10PT | 12PT | 16PT |
|---|---|---|---|---|---|---|---|
| ppem | 8 | 12 | 16 | 17 | 20 | 25 | 30 |

If you retune a size, re-check the golden screenshots and the tight spots
(`FreqHero` at 128 px, the GPS/Info value columns, menu-row truncation).

## Icons

A curated FontAwesome subset is merged into every font. Use the icons like text:

```c
gfx_print(pos, FONT_SIZE_8PT, TEXT_ALIGN_LEFT, color, SYMBOL_WIFI " " SYMBOL_GPS);
```

To add an icon: append its FontAwesome codepoint to `ICONS` in
`scripts/gen_fonts.sh`, add a matching `SYMBOL_*` macro in `graphics.h` (the
UTF-8 encoding of the codepoint), regenerate, and commit.

## Custom icon font (fallback layer)

App-specific marks that are **not** FontAwesome live in a separate *custom icon
font* rather than being merged into the text blobs: the M17 wordmark logo
(`SYMBOL_M17`, U+E900) and the Latin-1 supplement (U+00A1–U+00FF: accented
letters + symbols) that backs the UTF-8 character picker (`PickerView`). Each text font is given the
same-size custom-icon font as a `fallback` (mirroring LVGL's
`lv_font_t.fallback`), so `lvFont_getGlyph` walks the chain and those code
points render like any other glyph — a single-colour, theme-tinted silhouette
that also works on 1bpp targets. Keeping it separate means the `ubuntu_*.bin`
blobs stay byte-for-byte stable when the icon set changes, and vice-versa. The
per-size blobs are `openrtx/fonts/icons_*.bin`, embedded alongside the text
fonts in `fontData.S` and wired as fallbacks in `graphics.c` (`get_font`).

The M17 glyph is a UI-layer name (`SYMBOL_M17` in `src/ui/style/Symbols.hpp`,
not `graphics.h`). Its source is a 3-colour *stroked* SVG
(`openrtx/fonts/m17_logo.svg`, the M17 project logo as distributed in
M17-Project/ref-dash under GPL-3.0). `gen_fonts.sh`
flattens the strokes to fills (Inkscape `object-stroke-to-path`), drops the grey
drop-shadow layer, and wraps the foreground in a one-glyph TTF via
`scripts/svg_to_glyph_font.py` (fonttools) before baking it at every size. So
regenerating the icon font needs `inkscape` and `python3-fonttools` on top of
`npx`.

To add a custom icon: give it a Private-Use code point, add its SVG to the
`gen_fonts.sh` icon-font step (extend `svg_to_glyph_font.py` for multiple
glyphs), add a `SYMBOL_*` macro in `Symbols.hpp`, regenerate, and commit.

## UTF-8 and non-Latin (roadmap)

The text path decodes UTF-8, and the decoder reads the font `cmap` generically
(all four `lv_font_conv` subtable formats), so a font containing non-Latin
ranges already renders. What is not done yet: shipping such fonts (the committed
set is ASCII + icons only) and **loading font blobs at runtime** from
storage — the natural next step for CJK, where compiling every glyph into flash
is impractical. The `.bin` pipeline and `lvFont_init(data, size)` entry point
are the enablers already in place.
