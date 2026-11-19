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
struct lvFont {
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
};

/** A located glyph: geometry plus a pointer to the start of its pixel bits. */
struct lvGlyph {
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
};

/** Parse a font blob header and locate its tables. Returns false on a malformed
 *  or unsupported blob (leaves f->valid = false). */
bool lvFont_init(struct lvFont *f, const uint8_t *data, uint32_t size);

/** Resolve a Unicode codepoint to its glyph geometry. Returns false when the
 *  font has no glyph for `code` (the caller should skip it). */
bool lvFont_getGlyph(const struct lvFont *f, uint32_t code, struct lvGlyph *g);

/** Alpha (0..255) of pixel (x,y) within a glyph box, for RAW (uncompressed)
 *  fonts — 1bpp fonts are always raw. Out-of-range returns 0. */
uint8_t lvGlyph_pixelRaw(const struct lvFont *f, const struct lvGlyph *g,
                         uint16_t x, uint16_t y);

/** Widest glyph, in pixels, that lvGlyph_beginRows() decodes. */
#define LV_GLYPH_MAX_WIDTH 64

/**
 * State of decoding a glyph's pixels one row at a time, so a caller needs a
 * row of coverage rather than the whole glyph.
 */
struct lvGlyphRows {
    const uint8_t *in;   //< glyph bitstream
    uint32_t rdp;        //< bit position of the next pixel value in *in
    int32_t cnt;         //< RLE repeat counter
    uint16_t w;          //< glyph width in pixels
    uint8_t bpp;         //< bits per pixel
    uint8_t compression; //< 0 = raw, 1 = RLE+prefilter, 2 = RLE
    uint8_t state;       //< RLE decoder state
    uint8_t prev;        //< last RLE pixel value
    bool started;        //< false until the first pixel has been read
    bool first;          //< no row decoded yet
    uint8_t line[LV_GLYPH_MAX_WIDTH]; //< last row's values, for the prefilter
};

/**
 * Start decoding a glyph's pixels row by row, from raw or RLE-compressed
 * (anti-aliased) fonts.
 *
 * @param f: font the glyph was looked up in.
 * @param g: glyph.
 * @param r: decoder state to start.
 * @return false on a zero-size glyph or one wider than LV_GLYPH_MAX_WIDTH.
 */
bool lvGlyph_beginRows(const struct lvFont *f, const struct lvGlyph *g,
                       struct lvGlyphRows *r);

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
