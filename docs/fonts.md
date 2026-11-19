# UI fonts

OpenRTX's UI text is drawn from **LVGL binary fonts** (the format produced by
[`lv_font_conv`](https://github.com/lvgl/lv_font_conv)). The generated `.bin`
blobs are committed under `openrtx/fonts/` and embedded into the firmware; a
small in-tree reader decodes them at runtime. Its RLE decoder is ported from
LVGL's `lv_font_fmt_txt.c` (MIT).

## Layout

| Piece | Where |
|---|---|
| Committed text font blobs | `openrtx/fonts/ubuntu_<pt>_<bpp>.bin` |
| Committed icon font blobs | `openrtx/fonts/icons_<pt>_<bpp>.bin` |
| Regeneration script | `scripts/gen_fonts.sh` |
| Decoder (format reader + RLE) | `openrtx/src/core/fonts/lv_font_bin.{c,h}` |
| Embedding (`.incbin`) | `openrtx/src/core/fontData.c` |
| Text rendering | `openrtx/src/core/graphics.c`, `core/graphics.h` |
| Glyph names (`SYMBOL_*`) | `openrtx/src/ui/style/Symbols.hpp` |

Each `fontSize_t` (`FONT_SIZE_5PT` … `FONT_SIZE_16PT`) maps to one text blob
and one icon blob. Colour (RGB565) targets embed the anti-aliased **4bpp**
set; monochrome (BW) targets embed the compact **1bpp** set, which colour
targets also embed for the **Crisp** text setting. The choice is made in `fontData.c` from
`CONFIG_FONT_AA`, which the colour targets define in their `hwconfig.h` (the
colour emulator derives it from its pixel format).

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

`lv_font_conv` runs via `npx` at the version pinned in `gen_fonts.sh`, so only
Node.js is needed (no global install).

> **Gotcha:** ninja does not track `.incbin` dependencies. After regenerating
> the blobs, `touch openrtx/src/core/fontData.c` (or clean) so the embed is
> rebuilt.

## "pt" is a slot name, not a size — the ppem calibration

`lv_font_conv --size N` sizes a font to **N pixels** (ppem), which is **not**
the point value in the `FONT_SIZE_*PT` name. `gen_fonts.sh` pins a ppem per
slot, which sets each slot's digit advance and cap height; every layout is
sized against these:

| slot | 5PT | 6PT | 8PT | 9PT | 10PT | 12PT | 16PT |
|---|---|---|---|---|---|---|---|
| ppem | 8 | 12 | 16 | 17 | 20 | 25 | 30 |

If you retune a size, re-check the golden screenshots and the tight spots
(`FreqHero` at 128 px, the GPS/Info value columns, menu-row truncation).

## Icons

A curated FontAwesome subset is merged into every font. Use the icons like text:

```c
gfx_print(pos, FONT_SIZE_8PT, TEXT_ALIGN_LEFT, color, SYMBOL_LOCK " " SYMBOL_SIGNAL);
```

To add an icon: append its FontAwesome codepoint to `ICONS` in
`scripts/gen_fonts.sh`, add a matching `SYMBOL_*` macro in `Symbols.hpp` (the
UTF-8 encoding of the codepoint), regenerate, and commit.

## Custom icon font (fallback layer)

Glyphs that are not merged into the text fonts live in a separate *custom
icon font*, `openrtx/fonts/icons_*.bin`, so that changing them leaves the
`ubuntu_*.bin` blobs alone:

- the M17 wordmark logo (`SYMBOL_M17`, U+E900);
- the Latin-1 supplement (U+00A1–U+00FF) of the character picker
  (`PickerView`);
- the ellipsis (U+2026) that ends truncated text;
- the FontAwesome backspace (U+F55A) of the picker's delete slot.

The blobs therefore carry GPL-3.0, the Ubuntu Font Licence and the OFL (see
`REUSE.toml`). `get_font()` in `graphics.c` gives each text font the
same-size icon font as its `fallback`, as LVGL's `lv_font_t.fallback` does, so
`lvFont_getGlyph()` finds these glyphs like any other; they are drawn in the
text colour, on 1bpp targets too.

The M17 glyph is `SYMBOL_M17` in `Symbols.hpp`. Its source is a 3-colour
*stroked* SVG (`openrtx/fonts/m17_logo.svg`, the M17 project logo as
distributed in M17-Project/ref-dash under GPL-3.0). `gen_fonts.sh` flattens
the strokes to fills (Inkscape `object-stroke-to-path`), drops the grey
drop-shadow layer, and wraps the foreground in a one-glyph TTF via
`scripts/svg_to_glyph_font.py` (fonttools) before baking it at every size. So
regenerating the icon font needs `inkscape` and fonttools (`requirements.txt`)
on top of `npx`.

To add a custom icon: give it a Private-Use code point, add its SVG to the
`gen_fonts.sh` icon-font step (extend `svg_to_glyph_font.py` for multiple
glyphs), add a `SYMBOL_*` macro in `Symbols.hpp`, regenerate, and commit.

## UTF-8

The text path decodes UTF-8, and the decoder reads all four `lv_font_conv`
cmap subtable formats, so any code point a blob holds renders. The committed
blobs hold ASCII, Latin-1 (in the icon blobs) and the icons above.
`gfx_setFontSource()` installs a blob from outside the firmware in place of a
built-in one; it is the hook for installable font packs.
