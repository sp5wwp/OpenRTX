/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "lv_font_bin.h"
#include <string.h>

/*
 * Field offsets mirror lv_font_conv's table writers. Only the fields we need
 * are named; see lib/font/table_head.js for the full head layout.
 */

/* ---- little-endian scalar reads from the blob ---- */

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static int16_t rd_i16(const uint8_t *p)
{
    return (int16_t)rd_u16(p);
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/* ---- big-endian (MSB-first) bitstream reader over the glyf data ----
 * lv_font_conv writes glyph geometry with bit-buffer in big-endian mode, so
 * bit 0 of a value is the most significant bit of the first byte. */

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

/* Read an n-bit two's-complement value (used for the signed bbox x/y). */
static int32_t rd_bits_signed(const uint8_t *base, uint32_t bitpos, uint8_t n)
{
    uint32_t v = rd_bits(base, bitpos, n);
    if (n && (v & (1u << (n - 1))))
        return (int32_t)v - (int32_t)(1u << n);
    return (int32_t)v;
}

/* ---- header parse ---- */

bool lvFont_init(lvFont_t *f, const uint8_t *data, uint32_t size)
{
    memset(f, 0, sizeof(*f));
    if ((data == NULL) || (size < 12) || (memcmp(data + 4, "head", 4) != 0))
        return false;

    f->data = data;
    f->size = size;

    /* head table (see table_head.js for the exact offsets). */
    const uint8_t *h = data;
    f->ppem = rd_u16(h + 14);
    f->ascent = rd_i16(h + 16);
    f->descent = rd_i16(h + 18);
    f->default_adv = rd_u16(h + 30);
    f->adv_fmt = h[36];
    f->bpp = h[37];
    f->xy_bits = h[38];
    f->wh_bits = h[39];
    f->adv_bits = h[40];
    f->compression = h[41];
    f->loc_fmt = h[34];
    f->line_height = (uint16_t)(f->ascent - f->descent);

    /* Walk the remaining length-prefixed tables (cmap, loca, glyf). */
    uint32_t off = rd_u32(h); /* head table length */
    while ((off + 8u) <= size) {
        uint32_t tlen = rd_u32(data + off);
        const uint8_t *tag = data + off + 4;
        if (tlen == 0u || (off + tlen) > size)
            break;
        if (memcmp(tag, "cmap", 4) == 0)
            f->cmap_off = off;
        else if (memcmp(tag, "loca", 4) == 0) {
            f->loca_off = off;
            f->glyph_cnt = rd_u32(data + off + 8);
        } else if (memcmp(tag, "glyf", 4) == 0)
            f->glyf_off = off;
        off += tlen;
    }

    if (f->cmap_off == 0u || f->loca_off == 0u || f->glyf_off == 0u)
        return false;

    f->valid = true;
    return true;
}

/* ---- cmap: codepoint -> glyph id ----
 * cmap table: u32 len, "cmap", u32 subtable_count, then N 16-byte sub-headers,
 * then the sub-data blocks. Sub-header: u32 data_offset (from cmap start),
 * u32 range_start, u16 range_len, u16 glyph_id_offset, u16 entries, u8 format.
 * Formats (table_cmap.js): 0 = format0, 1 = sparse, 2 = format0_tiny,
 * 3 = sparse_tiny. Handling all four keeps non-contiguous / non-Latin ranges
 * (e.g. FontAwesome, future UTF-8) working, not just ASCII. */

static uint32_t cmap_lookup(const lvFont_t *f, uint32_t code)
{
    const uint8_t *c = f->data + f->cmap_off;
    uint32_t count = rd_u32(c + 8);

    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *sh = c + 12 + i * 16;
        uint32_t data_off = rd_u32(sh + 0);
        uint32_t range_start = rd_u32(sh + 4);
        uint16_t range_len = rd_u16(sh + 8);
        uint16_t gid_off = rd_u16(sh + 10);
        uint16_t entries = rd_u16(sh + 12);
        uint8_t fmt = sh[14];

        if (code < range_start || code >= (range_start + range_len))
            continue;

        uint32_t rel = code - range_start;
        const uint8_t *sd = c + data_off;

        switch (fmt) {
            case 2:   /* format0_tiny: contiguous ids, no data */
                return gid_off + rel;
            case 0: { /* format0: one id-delta byte per code in the range */
                uint8_t delta = sd[rel];
                return (delta == 0u) ? 0u : (gid_off + delta);
            }
            case 3: { /* sparse_tiny: u16 code-deltas, contiguous ids */
                for (uint16_t e = 0; e < entries; e++) {
                    if (rd_u16(sd + e * 2) == rel)
                        return gid_off + e;
                }
                return 0u;
            }
            case 1: { /* sparse: u16 code-deltas then u16 id-deltas */
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
    return 0u; /* .notdef */
}

/* ---- glyph geometry ---- */

bool lvFont_getGlyph(const lvFont_t *f, uint32_t code, lvGlyph_t *g)
{
    if (!f->valid)
        return false;

    uint32_t gid = cmap_lookup(f, code);
    if (gid == 0u || gid >= f->glyph_cnt) {
        /* Not in this font: walk the fallback chain (LVGL-style), so an app or
         * icon font layered on the base text font answers transparently. */
        if (f->fallback != NULL)
            return lvFont_getGlyph(f->fallback, code, g);
        return false;
    }

    /* loca[gid] and loca[gid+1] give this glyph's byte span within glyf. The
     * loca table stores exactly glyph_cnt start-offsets with NO trailing end
     * sentinel, so the last glyph's end is the glyf table length, not
     * loca[glyph_cnt] (which is out of bounds — either glyf-header bytes or
     * 4-byte-alignment padding, the latter reading 0 and dropping the glyph). */
    const uint8_t *loca = f->data + f->loca_off + 12;
    const uint32_t glyf_len = rd_u32(f->data + f->glyf_off);
    const bool last = (gid + 1u >= f->glyph_cnt);
    uint32_t start, next;
    if (f->loc_fmt) {
        start = rd_u32(loca + gid * 4);
        next = last ? glyf_len : rd_u32(loca + (gid + 1) * 4);
    } else {
        start = rd_u16(loca + gid * 2);
        next = last ? glyf_len : rd_u16(loca + (gid + 1) * 2);
    }
    if (next <= start)
        return false; /* empty glyph (e.g. space handled via advance below) */

    const uint8_t *gd = f->data + f->glyf_off + start;
    uint32_t bit = 0;

    /* Bitstream header: [advance?][x][y][w][h], then the pixel bits. */
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

    g->src = f; //< decode with this font's params, even via a fallback
    g->bmp = gd;
    g->bmp_bit = bit;
    g->data_len = next - start;
    return true;
}

/* Note: a present glyph with no pixels (e.g. space) still has a header in the
 * stream, so getGlyph() returns true with box_w == box_h == 0 and a valid
 * advance — the blit loop draws nothing and advances normally. */

/* Scale a bpp-bit pixel value to 8-bit coverage (LVGL's OPA tables are the same
 * linear map, e.g. 4bpp -> value * 17). */
static uint8_t opa_scale(uint8_t v, uint8_t bpp)
{
    uint32_t maxv = (1u << bpp) - 1u;
    return (uint8_t)((v * 255u + maxv / 2u) / maxv);
}

uint8_t lvGlyph_pixelRaw(const lvFont_t *f, const lvGlyph_t *g, uint16_t x,
                         uint16_t y)
{
    /* Decode with the font the glyph came from (a fallback may differ from f). */
    if (g->src != NULL)
        f = g->src;
    if (x >= g->box_w || y >= g->box_h)
        return 0;
    uint32_t idx = (uint32_t)y * g->box_w + x;
    uint32_t v = rd_bits(g->bmp, g->bmp_bit + idx * f->bpp, f->bpp);
    return opa_scale((uint8_t)v, f->bpp);
}

/* ---- RLE (modified I3BN) decompressor ----
 * Ported from LVGL's lv_font_fmt_txt.c (rle_next / decompress). The glyph
 * bitstream stores a run when a pixel value repeats the previous one: a 1-bit
 * flag chain, collapsing to a 6-bit counter after 11 repeats. compression == 1
 * additionally XOR-prefilters each line against the previous one. */

enum { RLE_SINGLE = 0, RLE_REPEAT, RLE_COUNTER };

typedef struct {
    const uint8_t *in;
    uint32_t rdp; //< absolute bit position into *in
    uint8_t bpp;
    uint8_t state;
    uint8_t prev;
    int32_t cnt;
    bool started; //< false until the first pixel has been read
} rle_t;

static uint8_t rle_next(rle_t *r)
{
    uint8_t ret = 0;

    if (r->state == RLE_SINGLE) {
        ret = (uint8_t)rd_bits(r->in, r->rdp, r->bpp);
        if (r->started && (r->prev == ret)) {
            r->cnt = 0;
            r->state = RLE_REPEAT;
        }
        r->started = true;
        r->prev = ret;
        r->rdp += r->bpp;
    } else if (r->state == RLE_REPEAT) {
        uint8_t v = (uint8_t)rd_bits(r->in, r->rdp, 1);
        r->cnt++;
        r->rdp += 1;
        if (v == 1) {
            ret = r->prev;
            if (r->cnt == 11) { /* RLE_BIT_COLLAPSED_COUNT + 1 */
                r->cnt = (int32_t)rd_bits(r->in, r->rdp, 6);
                r->rdp += 6;
                if (r->cnt != 0) {
                    r->state = RLE_COUNTER;
                } else {
                    ret = (uint8_t)rd_bits(r->in, r->rdp, r->bpp);
                    r->prev = ret;
                    r->rdp += r->bpp;
                    r->state = RLE_SINGLE;
                }
            }
        } else {
            ret = (uint8_t)rd_bits(r->in, r->rdp, r->bpp);
            r->prev = ret;
            r->rdp += r->bpp;
            r->state = RLE_SINGLE;
        }
    } else { /* RLE_COUNTER */
        ret = r->prev;
        r->cnt--;
        if (r->cnt == 0) {
            ret = (uint8_t)rd_bits(r->in, r->rdp, r->bpp);
            r->prev = ret;
            r->rdp += r->bpp;
            r->state = RLE_SINGLE;
        }
    }
    return ret;
}

bool lvFont_decodeGlyph(const lvFont_t *f, const lvGlyph_t *g, uint8_t *out)
{
    /* Decode with the font the glyph came from (a fallback may differ from f). */
    if (g->src != NULL)
        f = g->src;
    uint16_t w = g->box_w, h = g->box_h;
    if (w == 0 || h == 0)
        return false;

    if (f->compression == 0) {
        /* Raw: bpp bits per pixel, row-major. */
        for (uint32_t i = 0; i < (uint32_t)w * h; i++) {
            uint32_t v = rd_bits(g->bmp, g->bmp_bit + i * f->bpp, f->bpp);
            out[i] = opa_scale((uint8_t)v, f->bpp);
        }
        return true;
    }

    /* Compressed: decode line by line, undoing the per-line XOR prefilter. */
    if (w > 64)
        return false; /* guard the fixed line buffers */
    const bool prefilter = (f->compression == 1);
    rle_t r = { g->bmp, g->bmp_bit, f->bpp, RLE_SINGLE, 0, 0, false };
    uint8_t line1[64], line2[64];

    for (uint16_t x = 0; x < w; x++)
        line1[x] = rle_next(&r);
    for (uint16_t x = 0; x < w; x++)
        out[x] = opa_scale(line1[x], f->bpp);

    for (uint16_t y = 1; y < h; y++) {
        if (prefilter) {
            for (uint16_t x = 0; x < w; x++)
                line2[x] = rle_next(&r);
            for (uint16_t x = 0; x < w; x++) {
                line1[x] ^= line2[x];
                out[(uint32_t)y * w + x] = opa_scale(line1[x], f->bpp);
            }
        } else {
            for (uint16_t x = 0; x < w; x++)
                line1[x] = rle_next(&r);
            for (uint16_t x = 0; x < w; x++)
                out[(uint32_t)y * w + x] = opa_scale(line1[x], f->bpp);
        }
    }
    return true;
}
