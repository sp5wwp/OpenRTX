/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef LV_FONT_BIN_H
#define LV_FONT_BIN_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reader for the LVGL binary font format emitted by `lv_font_conv --format bin`
 * (https://github.com/lvgl/lv_font_conv). This is an in-tree, from-scratch
 * decoder — no LVGL code is vendored; it mirrors the on-disk layout defined by
 * lv_font_conv's own table writers (lib/font/table_{head,cmap,loca,glyf}.js),
 * documented inline where each field is parsed.
 *
 * The blob is a sequence of length-prefixed tables (`head`, `cmap`, `loca`,
 * `glyf`), little-endian. Glyph geometry lives in a big-endian (MSB-first)
 * bitstream. Coordinates use the LVGL/baseline convention: a glyph's `ofs_y` is
 * the signed offset from the text baseline to the BOTTOM of the glyph box (the
 * opposite anchor to the old Adafruit GFXfont `yOffset`).
 */

/** A parsed font blob. Populated by lvFont_init(); holds only offsets into the
 *  embedded data, no glyph is decoded until lvFont_getGlyph(). */
typedef struct lvFont {
    const uint8_t *data; //< the whole .bin blob (borrowed, not owned)
    uint32_t size;       //< blob length in bytes

    /* Optional next font to consult for code points this one lacks, mirroring
     * LVGL's lv_font_t.fallback chain (used to layer an app/icon font onto the
     * base text font). Borrowed; NULL for no fallback. */
    const struct lvFont *fallback;

    uint16_t ppem;        //< nominal pixel size
    int16_t ascent;       //< pixels above the baseline
    int16_t descent;      //< pixels below the baseline (<= 0)
    uint16_t line_height; //< ascent - descent
    uint16_t default_adv; //< advance for monospaced fonts (px)

    uint32_t cmap_off;    //< byte offset of the cmap table
    uint32_t loca_off;    //< byte offset of the loca table
    uint32_t glyf_off;    //< byte offset of the glyf table
    uint32_t glyph_cnt;   //< number of loca entries (glyph ids)

    uint8_t bpp;          //< bits per pixel (1, 2, 3 or 4)
    uint8_t compression;  //< 0 = raw, 1 = RLE+prefilter, 2 = RLE (see decoder)
    uint8_t xy_bits;      //< bit width of the signed glyph bbox x/y fields
    uint8_t wh_bits;      //< bit width of the glyph bbox w/h fields
    uint8_t adv_bits; //< bit width of the per-glyph advance (0 = monospaced)
    uint8_t adv_fmt;  //< 0 = integer px, 1 = FP4.4
    uint8_t loc_fmt;  //< loca entry size: 0 = uint16, 1 = uint32
    bool valid;       //< true once successfully parsed
} lvFont_t;

/** A located glyph: geometry plus a pointer to the start of its pixel bits. */
typedef struct {
    /* The font the glyph was actually resolved from — the queried font or one
     * of its fallbacks. Pixel decoding must use this font's parameters (bpp,
     * compression, ...), so lvFont_decodeGlyph()/lvGlyph_pixelRaw() read it. */
    const struct lvFont *src;
    int16_t ofs_x;      //< baseline-left to glyph-box left (signed)
    int16_t ofs_y;      //< baseline to glyph-box BOTTOM (signed, LVGL anchor)
    uint16_t box_w;     //< glyph bitmap width in pixels
    uint16_t box_h;     //< glyph bitmap height in pixels
    uint16_t adv_w;     //< horizontal advance in pixels
    const uint8_t *bmp; //< base pointer for the pixel bitstream
    uint32_t bmp_bit;   //< bit offset of the first pixel within *bmp
    uint32_t data_len; //< bytes from the glyph start to the next glyph (for RLE)
} lvGlyph_t;

/** Parse a font blob header and locate its tables. Returns false on a malformed
 *  or unsupported blob (leaves f->valid = false). */
bool lvFont_init(lvFont_t *f, const uint8_t *data, uint32_t size);

/** Resolve a Unicode codepoint to its glyph geometry. Returns false when the
 *  font has no glyph for `code` (the caller should skip it). */
bool lvFont_getGlyph(const lvFont_t *f, uint32_t code, lvGlyph_t *g);

/** Alpha (0..255) of pixel (x,y) within a glyph box, for RAW (uncompressed)
 *  fonts — 1bpp fonts are always raw. Out-of-range returns 0. */
uint8_t lvGlyph_pixelRaw(const lvFont_t *f, const lvGlyph_t *g, uint16_t x,
                         uint16_t y);

/** Decode a glyph's pixels into `out` as 8-bit coverage (0..255), box_w*box_h
 *  bytes row-major. Handles both raw and RLE-compressed (anti-aliased) fonts.
 *  Returns false on a zero-size glyph. */
bool lvFont_decodeGlyph(const lvFont_t *f, const lvGlyph_t *g, uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif /* LV_FONT_BIN_H */
