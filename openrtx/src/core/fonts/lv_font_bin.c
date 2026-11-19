/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-FileCopyrightText: Copyright 2025 LVGL Kft
 *
 * SPDX-License-Identifier: GPL-3.0-or-later AND MIT
 */

#include "lv_font_bin.h"
#include <string.h>

/**
 * Byte offsets of the fields read from the head table, as written by
 * lv_font_conv's lib/font/table_head.js.
 */
enum lvHead {
    LV_HEAD_ASCENT = 16,      ///< i16 ascent
    LV_HEAD_DESCENT = 18,     ///< i16 descent
    LV_HEAD_DEFAULT_ADV = 30, ///< u16 advance of a monospaced font
    LV_HEAD_LOC_FMT = 34,     ///< u8 loca entry size
    LV_HEAD_ADV_FMT = 36,     ///< u8 advance format
    LV_HEAD_BPP = 37,         ///< u8 bits per pixel
    LV_HEAD_XY_BITS = 38,     ///< u8 bit width of the glyph box x/y
    LV_HEAD_WH_BITS = 39,     ///< u8 bit width of the glyph box w/h
    LV_HEAD_ADV_BITS = 40,    ///< u8 bit width of the glyph advance
    LV_HEAD_COMPRESSION = 41, ///< u8 compression
    LV_HEAD_SIZE = 48,        ///< Length of the head table
};

/**
 * Byte offsets within a length-prefixed table.
 */
enum lvTable {
    LV_TABLE_TAG = 4,      ///< Four-character tag, after the u32 length
    LV_TABLE_HDR = 8,      ///< Length and tag
    LV_TABLE_COUNT = 8,    ///< u32 entry count of the cmap and loca tables
    LV_TABLE_ENTRIES = 12, ///< First cmap sub-header or loca entry
};

/**
 * Byte offsets within a cmap sub-header.
 */
enum lvCmapSub {
    LV_CMAP_DATA_OFF = 0,   ///< u32 sub-table data offset from the cmap start
    LV_CMAP_RANGE = 4,      ///< u32 first code point of the range
    LV_CMAP_RANGE_LEN = 8,  ///< u16 code points in the range
    LV_CMAP_GLYPH_OFF = 10, ///< u16 glyph id of the first code point
    LV_CMAP_ENTRIES = 12,   ///< u16 entries of a sparse sub-table
    LV_CMAP_FORMAT = 14,    ///< u8 sub-table format
    LV_CMAP_SUB_SIZE = 16,  ///< Size of one sub-header
};

/**
 * Formats of a cmap sub-table, from lv_font_conv's lib/font/table_cmap.js.
 */
enum lvCmapFormat {
    LV_CMAP_FORMAT0 = 0,      ///< One id-delta byte per code point
    LV_CMAP_SPARSE = 1,       ///< u16 code deltas, then u16 id deltas
    LV_CMAP_FORMAT0_TINY = 2, ///< Contiguous ids, no data
    LV_CMAP_SPARSE_TINY = 3,  ///< u16 code deltas, contiguous ids
};

/**
 * Limits of the head fields the decoder handles.
 */
enum lvLimits {
    LV_FONT_MAX_BPP = 4,         ///< Widest pixel, in bits
    LV_FONT_MAX_COMPRESSION = 2, ///< Highest compression format
    LV_FONT_MAX_FIELD_BITS = 16, ///< Widest glyph header field, in bits
};

/**
 * Read a little-endian u16.
 *
 * @param p: first byte.
 * @return the value.
 */
static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

/**
 * Read a little-endian i16.
 *
 * @param p: first byte.
 * @return the value.
 */
static int16_t rd_i16(const uint8_t *p)
{
    return (int16_t)rd_u16(p);
}

/**
 * Read a little-endian u32.
 *
 * @param p: first byte.
 * @return the value.
 */
static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/**
 * Read an unsigned value from the glyph bitstream, which lv_font_conv writes
 * MSB first: bit 0 of a value is the top bit of its first byte.
 *
 * @param base: start of the bitstream.
 * @param bitpos: bit position of the value.
 * @param n: value width in bits.
 * @return the value.
 */
static uint32_t rd_bits(const uint8_t *base, uint32_t bitpos, uint8_t n)
{
    uint32_t v = 0;
    for (uint8_t i = 0; i < n; i++) {
        uint32_t p = bitpos + i;
        uint8_t bit = (base[p >> 3] >> (7 - (p & 7))) & 1u;
        v = (v << 1) | bit;
    }
    return v;
}

/**
 * Read a two's-complement value from the glyph bitstream.
 *
 * @param base: start of the bitstream.
 * @param bitpos: bit position of the value.
 * @param n: value width in bits.
 * @return the value.
 */
static int32_t rd_bits_signed(const uint8_t *base, uint32_t bitpos, uint8_t n)
{
    uint32_t v = rd_bits(base, bitpos, n);
    if (n && (v & (1u << (n - 1))))
        return (int32_t)v - (int32_t)(1u << n);
    return (int32_t)v;
}

bool lvFont_init(struct lvFont *f, const uint8_t *data, uint32_t size)
{
    memset(f, 0, sizeof(*f));
    if ((data == NULL) || (size < LV_HEAD_SIZE)
        || (memcmp(data + LV_TABLE_TAG, "head", 4) != 0))
        return false;

    const uint8_t *h = data;
    f->data = data;
    f->ascent = rd_i16(h + LV_HEAD_ASCENT);
    f->descent = rd_i16(h + LV_HEAD_DESCENT);
    f->default_adv = rd_u16(h + LV_HEAD_DEFAULT_ADV);
    f->adv_fmt = h[LV_HEAD_ADV_FMT];
    f->bpp = h[LV_HEAD_BPP];
    f->xy_bits = h[LV_HEAD_XY_BITS];
    f->wh_bits = h[LV_HEAD_WH_BITS];
    f->adv_bits = h[LV_HEAD_ADV_BITS];
    f->compression = h[LV_HEAD_COMPRESSION];
    f->loc_fmt = h[LV_HEAD_LOC_FMT];
    f->line_height = (uint16_t)(f->ascent - f->descent);

    /* Walk the remaining length-prefixed tables (cmap, loca, glyf). */
    uint32_t off = rd_u32(h); /* head table length */
    while (off <= (size - LV_TABLE_HDR)) {
        uint32_t tlen = rd_u32(data + off);
        const uint8_t *tag = data + off + LV_TABLE_TAG;
        if ((tlen < LV_TABLE_HDR) || (tlen > (size - off)))
            break;
        if (memcmp(tag, "cmap", 4) == 0)
            f->cmap_off = off;
        else if (memcmp(tag, "loca", 4) == 0) {
            f->loca_off = off;
            if (tlen >= LV_TABLE_ENTRIES)
                f->glyph_cnt = rd_u32(data + off + LV_TABLE_COUNT);
        } else if (memcmp(tag, "glyf", 4) == 0)
            f->glyf_off = off;
        off += tlen;
    }

    if ((f->cmap_off == 0u) || (f->loca_off == 0u) || (f->glyf_off == 0u))
        return false;

    f->valid = true;
    return true;
}

/**
 * Map a code point to its glyph id through the cmap sub-tables, in all four
 * formats, so non-contiguous ranges such as the icons resolve too.
 *
 * @param f: font.
 * @param code: code point.
 * @return the glyph id, 0 (.notdef) when the font lacks the code point.
 */
static uint32_t cmap_lookup(const struct lvFont *f, uint32_t code)
{
    const uint8_t *c = f->data + f->cmap_off;
    uint32_t count = rd_u32(c + LV_TABLE_COUNT);

    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *sh = c + LV_TABLE_ENTRIES + i * LV_CMAP_SUB_SIZE;
        uint32_t data_off = rd_u32(sh + LV_CMAP_DATA_OFF);
        uint32_t range_start = rd_u32(sh + LV_CMAP_RANGE);
        uint16_t range_len = rd_u16(sh + LV_CMAP_RANGE_LEN);
        uint16_t gid_off = rd_u16(sh + LV_CMAP_GLYPH_OFF);
        uint16_t entries = rd_u16(sh + LV_CMAP_ENTRIES);
        uint8_t fmt = sh[LV_CMAP_FORMAT];

        if ((code < range_start) || (code >= (range_start + range_len)))
            continue;

        uint32_t rel = code - range_start;
        const uint8_t *sd = c + data_off;

        switch (fmt) {
            case LV_CMAP_FORMAT0_TINY:
                return gid_off + rel;
            case LV_CMAP_FORMAT0: {
                uint8_t delta = sd[rel];
                return (delta == 0u) ? 0u : (gid_off + delta);
            }
            case LV_CMAP_SPARSE_TINY: {
                for (uint16_t e = 0; e < entries; e++) {
                    if (rd_u16(sd + e * 2) == rel)
                        return gid_off + e;
                }
                return 0u;
            }
            case LV_CMAP_SPARSE: {
                const uint8_t *ids = sd + entries * 2;
                for (uint16_t e = 0; e < entries; e++) {
                    if (rd_u16(sd + e * 2) == rel)
                        return gid_off + rd_u16(ids + e * 2);
                }
                return 0u;
            }
            default:
                return 0u;
        }
    }
    return 0u;
}

/**
 * Get the byte span of a glyph within the glyf table. The loca table stores
 * glyph_cnt start offsets and no end sentinel, so the last glyph ends at the
 * end of the glyf table.
 *
 * @param f: font.
 * @param gid: glyph id, below glyph_cnt.
 * @param start: offset of the glyph's first byte from the glyf table start.
 * @param next: offset just past the glyph's last byte.
 */
static void glyph_span(const struct lvFont *f, uint32_t gid, uint32_t *start,
                       uint32_t *next)
{
    const uint8_t *loca = f->data + f->loca_off + LV_TABLE_ENTRIES;
    const bool last = ((gid + 1u) >= f->glyph_cnt);

    if (f->loc_fmt) {
        *start = rd_u32(loca + gid * 4);
        *next = last ? rd_u32(f->data + f->glyf_off) :
                       rd_u32(loca + (gid + 1) * 4);
    } else {
        *start = rd_u16(loca + gid * 2);
        *next = last ? rd_u32(f->data + f->glyf_off) :
                       rd_u16(loca + (gid + 1) * 2);
    }
}

bool lvFont_getGlyph(const struct lvFont *f, uint32_t code, struct lvGlyph *g)
{
    if (!f->valid)
        return false;

    uint32_t gid = cmap_lookup(f, code);
    if ((gid == 0u) || (gid >= f->glyph_cnt)) {
        /* Not in this font: an icon font layered on the text font answers. */
        if (f->fallback != NULL)
            return lvFont_getGlyph(f->fallback, code, g);
        return false;
    }

    uint32_t start, next;
    glyph_span(f, gid, &start, &next);
    if (next <= start)
        return false;

    const uint8_t *gd = f->data + f->glyf_off + start;
    uint32_t bit = 0;

    /* Bitstream header: [advance?][x][y][w][h], then the pixel bits. A glyph
     * with no pixels (e.g. space) still has one, with a zero-size box. */
    if (f->adv_bits > 0) {
        uint32_t a = rd_bits(gd, bit, f->adv_bits);
        bit += f->adv_bits;
        g->adv_w = (uint16_t)(f->adv_fmt ? ((a + 8u) >> 4) : a);
    } else {
        g->adv_w = f->adv_fmt ? ((f->default_adv + 8u) >> 4) : f->default_adv;
    }

    g->ofs_x = (int16_t)rd_bits_signed(gd, bit, f->xy_bits);
    bit += f->xy_bits;
    g->ofs_y = (int16_t)rd_bits_signed(gd, bit, f->xy_bits);
    bit += f->xy_bits;
    g->box_w = (uint16_t)rd_bits(gd, bit, f->wh_bits);
    bit += f->wh_bits;
    g->box_h = (uint16_t)rd_bits(gd, bit, f->wh_bits);
    bit += f->wh_bits;

    g->src = f;
    g->bmp = gd;
    g->bmp_bit = bit;
    g->bmp_end = 8u * (next - start);
    return true;
}

/**
 * Check that the font's head fields have values the decoder handles.
 *
 * @param f: font.
 * @return true if they do.
 */
static bool head_ok(const struct lvFont *f)
{
    return (f->bpp >= 1u) && (f->bpp <= LV_FONT_MAX_BPP)
        && (f->compression <= LV_FONT_MAX_COMPRESSION)
        && (f->xy_bits <= LV_FONT_MAX_FIELD_BITS)
        && (f->wh_bits <= LV_FONT_MAX_FIELD_BITS)
        && (f->adv_bits <= LV_FONT_MAX_FIELD_BITS);
}

/**
 * Check that the cmap sub-headers, and the data of each sub-table, lie inside
 * the cmap table.
 *
 * @param f: font.
 * @return true if they do.
 */
static bool cmap_ok(const struct lvFont *f)
{
    const uint8_t *c = f->data + f->cmap_off;
    const uint32_t len = rd_u32(c);
    if (len < LV_TABLE_ENTRIES)
        return false;

    const uint32_t count = rd_u32(c + LV_TABLE_COUNT);
    if (count > ((len - LV_TABLE_ENTRIES) / LV_CMAP_SUB_SIZE))
        return false;

    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *sh = c + LV_TABLE_ENTRIES + i * LV_CMAP_SUB_SIZE;
        const uint32_t data_off = rd_u32(sh + LV_CMAP_DATA_OFF);
        const uint32_t entries = rd_u16(sh + LV_CMAP_ENTRIES);
        uint32_t bytes;

        switch (sh[LV_CMAP_FORMAT]) {
            case LV_CMAP_FORMAT0:
                bytes = rd_u16(sh + LV_CMAP_RANGE_LEN);
                break;
            case LV_CMAP_SPARSE:
                bytes = 4u * entries;
                break;
            case LV_CMAP_FORMAT0_TINY:
                bytes = 0u;
                break;
            case LV_CMAP_SPARSE_TINY:
                bytes = 2u * entries;
                break;
            default:
                return false;
        }

        if ((data_off > len) || (bytes > (len - data_off)))
            return false;
    }

    return true;
}

/**
 * Check that the loca table holds glyph_cnt entries and that each glyph lies
 * inside the glyf table and holds its header, and for a raw font its pixels.
 *
 * @param f: font.
 * @return true if they do.
 */
static bool glyphs_ok(const struct lvFont *f)
{
    const uint32_t loca_len = rd_u32(f->data + f->loca_off);
    const uint32_t entry = f->loc_fmt ? 4u : 2u;
    if ((loca_len < LV_TABLE_ENTRIES)
        || (f->glyph_cnt > ((loca_len - LV_TABLE_ENTRIES) / entry)))
        return false;

    const uint32_t glyf_len = rd_u32(f->data + f->glyf_off);
    const uint32_t box = f->adv_bits + (2u * f->xy_bits);
    const uint32_t hdr = box + (2u * f->wh_bits);

    for (uint32_t gid = 0; gid < f->glyph_cnt; gid++) {
        uint32_t start, next;
        glyph_span(f, gid, &start, &next);
        if (next <= start)
            continue; /* lvFont_getGlyph() never returns it */
        if (next > glyf_len)
            return false;

        const uint32_t bits = 8u * (next - start);
        if (hdr > bits)
            return false;
        if (f->compression != 0u)
            continue; /* rle_bits() stops at the glyph's end */

        const uint8_t *gd = f->data + f->glyf_off + start;
        const uint64_t w = rd_bits(gd, box, f->wh_bits);
        const uint64_t h = rd_bits(gd, box + f->wh_bits, f->wh_bits);
        if ((w * h * f->bpp) > (bits - hdr))
            return false;
    }

    return true;
}

bool lvFont_check(const struct lvFont *f)
{
    return f->valid && head_ok(f) && cmap_ok(f) && glyphs_ok(f);
}

/**
 * Scale a pixel value to 8-bit coverage, the same linear map as LVGL's
 * opacity tables (4bpp: value * 17).
 *
 * @param v: pixel value.
 * @param bpp: bits per pixel.
 * @return coverage, 0 to 255.
 */
static uint8_t opa_scale(uint8_t v, uint8_t bpp)
{
    uint32_t maxv = (1u << bpp) - 1u;
    return (uint8_t)((v * 255u + maxv / 2u) / maxv);
}

uint8_t lvGlyph_pixelRaw(const struct lvGlyph *g, uint16_t x, uint16_t y)
{
    if ((x >= g->box_w) || (y >= g->box_h))
        return 0;

    const uint8_t bpp = g->src->bpp;
    uint32_t idx = (uint32_t)y * g->box_w + x;
    uint32_t v = rd_bits(g->bmp, g->bmp_bit + idx * bpp, bpp);
    return opa_scale((uint8_t)v, bpp);
}

/* ---- RLE (modified I3BN) decompressor ----
 * Ported from rle_next() and decompress_line() in LVGL's
 * src/font/fmt_txt/lv_font_fmt_txt.c (https://github.com/lvgl/lvgl), MIT
 * licence. The glyph bitstream stores a run when a pixel value repeats the
 * previous one: a 1-bit flag chain, collapsing to a 6-bit counter after 11
 * repeats. compression == 1 additionally XOR-prefilters each line against the
 * previous one. */

/**
 * States of the RLE decoder.
 */
enum rleState {
    RLE_SINGLE = 0, ///< Reading a literal pixel value
    RLE_REPEAT,     ///< Reading the 1-bit repeat flags
    RLE_COUNTER,    ///< Counting down a collapsed run
};

/**
 * Shape of a run in the RLE bitstream.
 */
enum rleRun {
    RLE_COLLAPSE_COUNT = 11, ///< Repeat flags read before a run collapses
    RLE_COUNTER_BITS = 6,    ///< Width in bits of a collapsed run's counter
};

/**
 * Read the next value of an RLE bitstream. A compressed glyph's length is
 * known only once it is decoded, so a value past the glyph's data reads 0.
 *
 * @param r: decoder state.
 * @param n: value width in bits.
 * @return the value.
 */
static uint32_t rle_bits(struct lvGlyphRows *r, uint8_t n)
{
    uint32_t v = 0;
    if ((r->rdp <= r->end) && (n <= (r->end - r->rdp)))
        v = rd_bits(r->in, r->rdp, n);
    r->rdp += n;
    return v;
}

/**
 * Decode the next pixel value of an RLE-compressed glyph.
 *
 * @param r: decoder state.
 * @return the pixel value.
 */
static uint8_t rle_next(struct lvGlyphRows *r)
{
    uint8_t ret = 0;

    if (r->state == RLE_SINGLE) {
        ret = (uint8_t)rle_bits(r, r->bpp);
        if (r->started && (r->prev == ret)) {
            r->cnt = 0;
            r->state = RLE_REPEAT;
        }
        r->started = true;
        r->prev = ret;
    } else if (r->state == RLE_REPEAT) {
        uint8_t v = (uint8_t)rle_bits(r, 1);
        r->cnt++;
        if (v == 1) {
            ret = r->prev;
            if (r->cnt == RLE_COLLAPSE_COUNT) {
                r->cnt = (int32_t)rle_bits(r, RLE_COUNTER_BITS);
                if (r->cnt != 0) {
                    r->state = RLE_COUNTER;
                } else {
                    ret = (uint8_t)rle_bits(r, r->bpp);
                    r->prev = ret;
                    r->state = RLE_SINGLE;
                }
            }
        } else {
            ret = (uint8_t)rle_bits(r, r->bpp);
            r->prev = ret;
            r->state = RLE_SINGLE;
        }
    } else { /* RLE_COUNTER */
        ret = r->prev;
        r->cnt--;
        if (r->cnt == 0) {
            ret = (uint8_t)rle_bits(r, r->bpp);
            r->prev = ret;
            r->state = RLE_SINGLE;
        }
    }
    return ret;
}

bool lvGlyph_beginRows(const struct lvGlyph *g, struct lvGlyphRows *r)
{
    if ((g->box_w == 0) || (g->box_h == 0) || (g->box_w > LV_GLYPH_MAX_WIDTH))
        return false;

    r->in = g->bmp;
    r->rdp = g->bmp_bit;
    r->end = g->bmp_end;
    r->cnt = 0;
    r->w = g->box_w;
    r->bpp = g->src->bpp;
    r->compression = g->src->compression;
    r->state = RLE_SINGLE;
    r->prev = 0;
    r->started = false;
    r->first = true;
    return true;
}

void lvGlyph_nextRow(struct lvGlyphRows *r, uint8_t *out)
{
    if (r->compression == 0) {
        /* Raw: bpp bits per pixel, row-major. */
        for (uint16_t x = 0; x < r->w; x++) {
            uint32_t v = rd_bits(r->in, r->rdp, r->bpp);
            r->rdp += r->bpp;
            out[x] = opa_scale((uint8_t)v, r->bpp);
        }
        return;
    }

    /* Compressed: every row after the first is XOR-prefiltered against the
     * previous one when compression == 1. */
    const bool prefilter = (r->compression == 1) && !r->first;
    for (uint16_t x = 0; x < r->w; x++) {
        const uint8_t v = rle_next(r);
        r->line[x] = prefilter ? (uint8_t)(r->line[x] ^ v) : v;
        out[x] = opa_scale(r->line[x], r->bpp);
    }
    r->first = false;
}
