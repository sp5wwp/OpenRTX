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
 * (https://github.com/lvgl/lv_font_conv). The tables are parsed as laid out by
 * lv_font_conv's writers (lib/font/table_{head,cmap,loca,glyf}.js); the RLE
 * pixel decoder in lv_font_bin.c is ported from LVGL (MIT).
 *
 * The blob is a sequence of length-prefixed tables (`head`, `cmap`, `loca`,
 * `glyf`), little-endian. Glyph geometry lives in a big-endian (MSB-first)
 * bitstream. A glyph's `ofs_y` is the signed offset from the text baseline to
 * the BOTTOM of the glyph box.
 */

/**
 * A parsed font blob: offsets into the embedded data, filled by lvFont_init().
 * No glyph is decoded until lvFont_getGlyph().
 */
struct lvFont {
    const uint8_t *data;           ///< The whole blob, borrowed
    const struct lvFont *fallback; ///< Font for code points this one lacks
    uint32_t cmap_off;             ///< Byte offset of the cmap table
    uint32_t loca_off;             ///< Byte offset of the loca table
    uint32_t glyf_off;             ///< Byte offset of the glyf table
    uint32_t glyph_cnt;            ///< Number of loca entries (glyph ids)
    int16_t ascent;                ///< Pixels above the baseline
    int16_t descent;               ///< Pixels below the baseline, <= 0
    uint16_t line_height;          ///< Ascent minus descent
    uint16_t default_adv;          ///< Advance of a monospaced font, pixels
    uint8_t bpp;                   ///< Bits per pixel: 1, 2, 3 or 4
    uint8_t compression;           ///< 0 raw, 1 RLE and prefilter, 2 RLE
    uint8_t xy_bits;               ///< Bit width of the glyph box x/y
    uint8_t wh_bits;               ///< Bit width of the glyph box w/h
    uint8_t adv_bits;              ///< Bit width of the advance, 0 monospaced
    uint8_t adv_fmt;               ///< 0 integer pixels, 1 FP4.4
    uint8_t loc_fmt;               ///< Loca entry size: 0 u16, 1 u32
    bool valid;                    ///< Parsed successfully
};

/**
 * A located glyph: geometry plus a pointer to the start of its pixel bits.
 */
struct lvGlyph {
    const struct lvFont *src; ///< Font it was found in, whose bpp applies
    const uint8_t *bmp;       ///< Base pointer of the pixel bitstream
    uint32_t bmp_bit;         ///< Bit offset of the first pixel in bmp
    int16_t ofs_x;            ///< Baseline-left to glyph box left
    int16_t ofs_y;            ///< Baseline to glyph box BOTTOM
    uint16_t box_w;           ///< Glyph bitmap width, pixels
    uint16_t box_h;           ///< Glyph bitmap height, pixels
    uint16_t adv_w;           ///< Horizontal advance, pixels
};

/**
 * Parse a font blob's head and locate its tables.
 *
 * @param f: font to fill.
 * @param data: blob, borrowed.
 * @param size: blob length in bytes.
 * @return false, leaving f->valid false, on a malformed or unsupported blob.
 */
bool lvFont_init(struct lvFont *f, const uint8_t *data, uint32_t size);

/**
 * Look up a code point, in the font or its fallbacks.
 *
 * @param f: font.
 * @param code: Unicode code point.
 * @param g: glyph found.
 * @return false when no font in the chain has the code point.
 */
bool lvFont_getGlyph(const struct lvFont *f, uint32_t code, struct lvGlyph *g);

/**
 * Get one pixel of a glyph from an uncompressed font; 1bpp fonts always are.
 *
 * @param g: glyph.
 * @param x: column in the glyph box.
 * @param y: row in the glyph box.
 * @return coverage, 0 to 255; 0 outside the box.
 */
uint8_t lvGlyph_pixelRaw(const struct lvGlyph *g, uint16_t x, uint16_t y);

/** Widest glyph, in pixels, that lvGlyph_beginRows() decodes. */
#define LV_GLYPH_MAX_WIDTH 64

/**
 * State of decoding a glyph's pixels one row at a time, so a caller needs a
 * row of coverage rather than the whole glyph.
 */
struct lvGlyphRows {
    const uint8_t *in;                ///< Glyph bitstream
    uint32_t rdp;                     ///< Bit position of the next value
    int32_t cnt;                      ///< RLE repeat counter
    uint16_t w;                       ///< Glyph width, pixels
    uint8_t bpp;                      ///< Bits per pixel
    uint8_t compression;              ///< 0 raw, 1 RLE and prefilter, 2 RLE
    uint8_t state;                    ///< RLE decoder state
    uint8_t prev;                     ///< Last RLE pixel value
    bool started;                     ///< A pixel has been read
    bool first;                       ///< No row decoded yet
    uint8_t line[LV_GLYPH_MAX_WIDTH]; ///< Last row's values, for the prefilter
};

/**
 * Start decoding a glyph's pixels row by row, from raw or RLE-compressed
 * (anti-aliased) fonts.
 *
 * @param g: glyph.
 * @param r: decoder state to start.
 * @return false on a zero-size glyph or one wider than LV_GLYPH_MAX_WIDTH.
 */
bool lvGlyph_beginRows(const struct lvGlyph *g, struct lvGlyphRows *r);

/**
 * Decode the next row of a glyph as 8-bit coverage (0..255).
 *
 * @param r: decoder state from lvGlyph_beginRows().
 * @param out: output, box_w bytes.
 */
void lvGlyph_nextRow(struct lvGlyphRows *r, uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif /* LV_FONT_BIN_H */
