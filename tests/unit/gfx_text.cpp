/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <climits>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" {
#include "core/graphics.h"
#include "fonts/lv_font_bin.h"
}

/*
 * Line-height of the test font (FONT_SIZE_5PT). Queried at runtime so the tests
 * stay valid across font changes (the layout logic is what's under test, not a
 * particular font's metrics).
 */
static uint16_t lineH()
{
    return gfx_getFontLineHeight(FONT_SIZE_5PT);
}

/* A max_x wide enough that no ASCII string of a few characters will wrap. */
static constexpr uint16_t WIDE = 4096;

/* A max_x so narrow (1 px) that every printable glyph triggers a wrap. */
static constexpr uint16_t NARROW = 1;

/* -----------------------------------------------------------------------
 * gfx_printBuffer regression tests.
 *
 * These pin the behaviour fixed in the commit that corrected wrap logic,
 * UB glyph access order, and the sign of text_size.y.
 * ----------------------------------------------------------------------- */

TEST_CASE("gfx_printBuffer text_size.y is positive", "[gfx][text]")
{
    color_t white = { 255, 255, 255, 255 };
    point_t start = { 0, 10 };

    point_t sz = gfx_printBuffer(start, FONT_SIZE_5PT, TEXT_ALIGN_LEFT, white,
                                 "Hello");
    REQUIRE(sz.y > 0);
}

TEST_CASE("gfx_printBuffer text_size.y grows with newlines", "[gfx][text]")
{
    color_t white = { 255, 255, 255, 255 };
    point_t start = { 0, 10 };

    point_t sz1 = gfx_printBuffer(start, FONT_SIZE_5PT, TEXT_ALIGN_LEFT, white,
                                  "Hello");
    point_t sz2 = gfx_printBuffer(start, FONT_SIZE_5PT, TEXT_ALIGN_LEFT, white,
                                  "Hello\nHello");

    REQUIRE(sz2.y > sz1.y);
    REQUIRE((sz2.y - sz1.y) == lineH());
}

TEST_CASE("gfx_printBuffer does not access glyph for newline", "[gfx][text]")
{
    /* If the UB glyph-access-before-newline-check bug were present, this
     * would either crash or corrupt memory when '\\n' (0x0A) is below
     * f.first.  Reaching REQUIRE without fault is the assertion. */
    color_t white = { 255, 255, 255, 255 };
    point_t start = { 0, 10 };

    point_t sz = gfx_printBuffer(start, FONT_SIZE_5PT, TEXT_ALIGN_LEFT, white,
                                 "a\nb\nc");
    REQUIRE(sz.y > 0);
}

TEST_CASE("gfx_measureText single-line strings", "[gfx][text]")
{
    SECTION("empty string occupies one line")
    {
        uint16_t h = gfx_measureText(FONT_SIZE_5PT, "", 0, WIDE, SIZE_MAX);
        REQUIRE(h == lineH());
    }

    SECTION("short string with no wrap occupies one line")
    {
        uint16_t h = gfx_measureText(FONT_SIZE_5PT, "Hello", 0, WIDE, SIZE_MAX);
        REQUIRE(h == lineH());
    }

    SECTION("char_count zero treats string as empty")
    {
        uint16_t h = gfx_measureText(FONT_SIZE_5PT, "Hello", 0, WIDE, 0);
        REQUIRE(h == lineH());
    }
}

TEST_CASE("gfx_measureText multi-line via explicit newlines", "[gfx][text]")
{
    SECTION("one newline produces two lines")
    {
        uint16_t h = gfx_measureText(FONT_SIZE_5PT, "abc\ndef", 0, WIDE,
                                     SIZE_MAX);
        REQUIRE(h == 2 * lineH());
    }

    SECTION("two newlines produce three lines")
    {
        uint16_t h = gfx_measureText(FONT_SIZE_5PT, "a\nb\nc", 0, WIDE,
                                     SIZE_MAX);
        REQUIRE(h == 3 * lineH());
    }

    SECTION("trailing newline adds an extra line")
    {
        uint16_t h = gfx_measureText(FONT_SIZE_5PT, "abc\n", 0, WIDE, SIZE_MAX);
        REQUIRE(h == 2 * lineH());
    }
}

TEST_CASE("gfx_measureText char_count truncation", "[gfx][text]")
{
    SECTION("truncate before newline stays on first line")
    {
        /* "abc\ndef" with char_count 3 — newline never reached */
        uint16_t h = gfx_measureText(FONT_SIZE_5PT, "abc\ndef", 0, WIDE, 3);
        REQUIRE(h == lineH());
    }

    SECTION("truncate including newline advances to second line")
    {
        /* char_count 4 includes the '\\n' */
        uint16_t h = gfx_measureText(FONT_SIZE_5PT, "abc\ndef", 0, WIDE, 4);
        REQUIRE(h == 2 * lineH());
    }

    SECTION("char_count larger than string length measures whole string")
    {
        uint16_t h_full = gfx_measureText(FONT_SIZE_5PT, "hi", 0, WIDE,
                                          SIZE_MAX);
        uint16_t h_over = gfx_measureText(FONT_SIZE_5PT, "hi", 0, WIDE, 999);
        REQUIRE(h_over == h_full);
    }
}

TEST_CASE("gfx_measureText word-wrap at max_x", "[gfx][text]")
{
    SECTION("each glyph wraps with max_x=1, height grows per character")
    {
        /*
         * With max_x=1 every printable glyph triggers the wrap condition
         * (cur_x + xAdvance > 1), adding one yAdvance per character.
         * One character → initial line (yAdvance) + one wrap (yAdvance).
         */
        uint16_t h1 = gfx_measureText(FONT_SIZE_5PT, "A", 0, NARROW, SIZE_MAX);
        uint16_t h2 = gfx_measureText(FONT_SIZE_5PT, "AB", 0, NARROW, SIZE_MAX);
        REQUIRE(h1 == 2 * lineH());
        REQUIRE(h2 == h1 + lineH());
    }

    SECTION("wide max_x prevents wrap for short string")
    {
        uint16_t h_wide = gfx_measureText(FONT_SIZE_5PT, "Hello", 0, WIDE,
                                          SIZE_MAX);
        uint16_t h_narrow = gfx_measureText(FONT_SIZE_5PT, "Hello", 0, NARROW,
                                            SIZE_MAX);
        REQUIRE(h_narrow > h_wide);
    }
}

/* -----------------------------------------------------------------------
 * gfx_printBufferClipped return-value tests.
 *
 * gfx_setPixel is a no-op in the Linux platform test build, so we cannot
 * verify pixel output.  We verify the returned text_size instead, which
 * exercises the layout logic without display hardware.
 * ----------------------------------------------------------------------- */

TEST_CASE("gfx_printBufferClipped text_size.y for single-line text",
          "[gfx][text]")
{
    color_t white = { 255, 255, 255, 255 };

    /*
     * Single-line: start.y does not advance, so text_size.y should equal
     * the raw glyph height of the last character rendered (line_h).  It
     * must be > 0 and <= lineH().
     */
    point_t start = { 0, 20 };
    point_t sz = gfx_printBufferClipped(start, FONT_SIZE_5PT, TEXT_ALIGN_LEFT,
                                        white, "Hi", (uint16_t)WIDE, 0, 127);
    REQUIRE(sz.y > 0);
    REQUIRE(sz.y <= lineH());
}

TEST_CASE("gfx_printBufferClipped text_size.y grows for two lines",
          "[gfx][text]")
{
    color_t white = { 255, 255, 255, 255 };
    point_t start = { 0, 20 };

    point_t sz1 = gfx_printBufferClipped(start, FONT_SIZE_5PT, TEXT_ALIGN_LEFT,
                                         white, "Hi", (uint16_t)WIDE, 0, 127);
    point_t sz2 = gfx_printBufferClipped(start, FONT_SIZE_5PT, TEXT_ALIGN_LEFT,
                                         white, "Hi\nHi", (uint16_t)WIDE, 0,
                                         127);

    /* Two lines must be taller than one. */
    REQUIRE(sz2.y > sz1.y);
    /* The difference should equal exactly one yAdvance. */
    REQUIRE((sz2.y - sz1.y) == lineH());
}

TEST_CASE("gfx_printBufferClipped negative start.y does not corrupt result",
          "[gfx][text]")
{
    color_t white = { 255, 255, 255, 255 };

    /* Scrolled position: top of text block is above the visible area. */
    point_t start_neg = { 0, -10 };
    point_t start_pos = { 0, 0 };

    point_t sz_neg = gfx_printBufferClipped(start_neg, FONT_SIZE_5PT,
                                            TEXT_ALIGN_LEFT, white, "Hi",
                                            (uint16_t)WIDE, 0, 127);
    point_t sz_pos = gfx_printBufferClipped(start_pos, FONT_SIZE_5PT,
                                            TEXT_ALIGN_LEFT, white, "Hi",
                                            (uint16_t)WIDE, 0, 127);

    /* Layout is the same regardless of vertical scroll offset. */
    REQUIRE(sz_neg.y == sz_pos.y);
    REQUIRE(sz_neg.y > 0);
}

TEST_CASE("gfx_printBufferClipped negative clip_top_y does not corrupt result",
          "[gfx][text]")
{
    color_t white = { 255, 255, 255, 255 };

    /* Pixels above row 0 must be suppressed without corrupting layout. */
    point_t start = { 0, 10 };
    point_t sz = gfx_printBufferClipped(start, FONT_SIZE_5PT, TEXT_ALIGN_LEFT,
                                        white, "Hi", (uint16_t)WIDE, -10, 127);
    REQUIRE(sz.y > 0);
    REQUIRE(sz.y <= lineH());
}

TEST_CASE("get_line_size exact-fit glyph is included in line width",
          "[gfx][text]")
{
    color_t white = { 255, 255, 255, 255 };
    point_t start = { 0, 10 };

    /* With max_x equal to 'A's advance, the glyph exactly fills the line. */
    uint16_t adv = gfx_getTextWidth(FONT_SIZE_5PT, "A");
    point_t sz = gfx_printBufferClipped(start, FONT_SIZE_5PT, TEXT_ALIGN_LEFT,
                                        white, "A", adv, 0, 127);
    REQUIRE(sz.x == adv);
}

TEST_CASE("gfx_printBufferClipped text_size.x is max line width, not last line",
          "[gfx][text]")
{
    color_t white = { 255, 255, 255, 255 };
    point_t start = { 0, 10 };

    /*
     * First line "Hello" is wider than second line "Hi".
     * text_size.x must reflect the wider first line, not the shorter last.
     */
    point_t sz_wide = gfx_printBufferClipped(start, FONT_SIZE_5PT,
                                             TEXT_ALIGN_LEFT, white, "Hello",
                                             (uint16_t)WIDE, 0, 127);
    point_t sz_multi =
        gfx_printBufferClipped(start, FONT_SIZE_5PT, TEXT_ALIGN_LEFT, white,
                               "Hello\nHi", (uint16_t)WIDE, 0, 127);
    REQUIRE(sz_multi.x == sz_wide.x);
}

TEST_CASE("gfx_printBufferClipped text_size.x when second line is wider",
          "[gfx][text]")
{
    color_t white = { 255, 255, 255, 255 };
    point_t start = { 0, 10 };

    /* Second line "Hello" is wider than first line "Hi". */
    point_t sz_wide = gfx_printBufferClipped(start, FONT_SIZE_5PT,
                                             TEXT_ALIGN_LEFT, white, "Hello",
                                             (uint16_t)WIDE, 0, 127);
    point_t sz_multi =
        gfx_printBufferClipped(start, FONT_SIZE_5PT, TEXT_ALIGN_LEFT, white,
                               "Hi\nHello", (uint16_t)WIDE, 0, 127);
    REQUIRE(sz_multi.x == sz_wide.x);
}

TEST_CASE("gfx_printBufferClipped glyph at clip_bot_y baseline is not skipped",
          "[gfx][text]")
{
    color_t white = { 255, 255, 255, 255 };

    /* Baseline at clip_bot_y+1: all glyph pixels still fall within window. */
    int16_t base_y = 20;
    int16_t clip_bot = base_y - 1;
    point_t start = { 0, base_y };

    point_t sz = gfx_printBufferClipped(start, FONT_SIZE_5PT, TEXT_ALIGN_LEFT,
                                        white, "A", (uint16_t)WIDE, 0,
                                        clip_bot);
    REQUIRE(sz.y > 0);
}

TEST_CASE("gfx_printBufferClipped TEXT_ALIGN_RIGHT resets per line",
          "[gfx][text]")
{
    color_t white = { 255, 255, 255, 255 };
    point_t start = { 0, 10 };

    /*
     * With TEXT_ALIGN_RIGHT each line's start.x should be computed from
     * that line's own width.  The returned text_size.x must equal the
     * width of the widest line, same as for LEFT alignment.
     */
    point_t sz_right =
        gfx_printBufferClipped(start, FONT_SIZE_5PT, TEXT_ALIGN_RIGHT, white,
                               "Hello\nHi", (uint16_t)WIDE, 0, 127);
    point_t sz_left =
        gfx_printBufferClipped(start, FONT_SIZE_5PT, TEXT_ALIGN_LEFT, white,
                               "Hello\nHi", (uint16_t)WIDE, 0, 127);
    /* Max line width is alignment-independent. */
    REQUIRE(sz_right.x == sz_left.x);
}

/* -----------------------------------------------------------------------
 * gfx_scrollOffsetForCursor
 *
 * The test strings repeat a single glyph, so every character advances by the
 * same amount whatever the font. With start_x 0 and max_x set to six advances
 * exactly six characters fit on a line and the expected geometry below is
 * exact rather than approximate. Metrics are queried at runtime, like lineH().
 * ----------------------------------------------------------------------- */

static uint16_t sixCols()
{
    return gfx_getTextWidth(FONT_SIZE_5PT, "A") * 6;
}

static int16_t twoLines()
{
    return (int16_t)(lineH() * 2);
}

/* 52 characters: the longest meta text the M17 specification allows. */
static const char META52[] =
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

static int16_t metaOffset(size_t cursor_pos, int16_t visible_h)
{
    return gfx_scrollOffsetForCursor(FONT_SIZE_5PT, META52, cursor_pos, 0,
                                     sixCols(), visible_h);
}

TEST_CASE("gfx_scrollOffsetForCursor does not scroll text that fits",
          "[gfx][scroll]")
{
    REQUIRE(gfx_scrollOffsetForCursor(FONT_SIZE_5PT, "ABC", 2, 0, sixCols(),
                                      twoLines())
            == 0);
}

TEST_CASE("gfx_scrollOffsetForCursor does not scroll while the cursor is "
          "inside the window",
          "[gfx][scroll]")
{
    /* Six characters per line, two lines visible: the first twelve
     * characters need no scrolling. */
    for (size_t pos = 0; pos < 12; pos++) {
        INFO("cursor position " << pos);
        REQUIRE(metaOffset(pos, twoLines()) == 0);
    }
}

TEST_CASE("gfx_scrollOffsetForCursor scrolls by the amount the cursor "
          "overflows the window",
          "[gfx][scroll]")
{
    /* Character 12 starts the third line, one line below a two-line
     * window; character 18 starts the fourth, two lines below. */
    REQUIRE(metaOffset(12, twoLines()) == (int16_t)lineH());
    REQUIRE(metaOffset(18, twoLines()) == (int16_t)(lineH() * 2));
}

TEST_CASE("gfx_scrollOffsetForCursor never scrolls backwards as the cursor "
          "advances",
          "[gfx][scroll]")
{
    int16_t previous = 0;

    for (size_t pos = 0; pos < sizeof(META52) - 1; pos++) {
        int16_t offset = metaOffset(pos, twoLines());
        INFO("cursor position " << pos);
        REQUIRE(offset >= previous);
        previous = offset;
    }
}

TEST_CASE("gfx_scrollOffsetForCursor keeps every position of a 52 character "
          "meta text visible",
          "[gfx][scroll]")
{
    /*
     * The property the meta text field depends on: whatever the cursor
     * position, the line holding it must fall inside the window once the
     * returned offset is applied.  Before the scroll helper the field was
     * drawn at a fixed offset, so everything past the last visible line was
     * simply not shown.
     */
    for (size_t pos = 0; pos < sizeof(META52) - 1; pos++) {
        uint16_t cursor_y = gfx_measureText(FONT_SIZE_5PT, META52, 0, sixCols(),
                                            pos + 1);
        int16_t visible_y = (int16_t)cursor_y - metaOffset(pos, twoLines());

        INFO("cursor position " << pos);
        REQUIRE(visible_y > 0);
        REQUIRE(visible_y <= twoLines());
    }
}

TEST_CASE("gfx_scrollOffsetForCursor edge cases", "[gfx][scroll]")
{
    SECTION("empty string never scrolls")
    {
        REQUIRE(gfx_scrollOffsetForCursor(FONT_SIZE_5PT, "", 0, 0, sixCols(),
                                          twoLines())
                == 0);
    }

    SECTION("cursor past the end of the string measures the whole string")
    {
        REQUIRE(metaOffset(sizeof(META52) * 2, twoLines())
                == metaOffset(sizeof(META52) - 2, twoLines()));
    }

    SECTION("window shorter than one line still scrolls")
    {
        REQUIRE(
            gfx_scrollOffsetForCursor(FONT_SIZE_5PT, "A", 0, 0, sixCols(), 1)
            > 0);
    }

    SECTION("SIZE_MAX cursor position does not wrap to zero")
    {
        REQUIRE(gfx_scrollOffsetForCursor(FONT_SIZE_5PT, META52, SIZE_MAX, 0,
                                          sixCols(), twoLines())
                > 0);
    }
}

TEST_CASE("gfx_printBufferClipped lays out the whole string, however long",
          "[gfx][scroll]")
{
    /*
     * What the meta text field relies on: the buffer entry points lay out
     * every character they are given and have no length limit of their own,
     * unlike the printf-style entry points which format into a shared
     * fixed-size buffer first.  Six characters fit on a line here, so a 200
     * character string takes 29 more line breaks than a 30 character one; were
     * the long string being cut short the two heights would converge.
     */
    color_t white = { 255, 255, 255, 255 };
    char longtext[201];
    char shorttext[31];

    memset(longtext, 'A', sizeof(longtext) - 1);
    longtext[sizeof(longtext) - 1] = '\0';
    memset(shorttext, 'A', sizeof(shorttext) - 1);
    shorttext[sizeof(shorttext) - 1] = '\0';

    point_t start = { 0, (int16_t)lineH() };
    point_t sz_long = gfx_printBufferClipped(start, FONT_SIZE_5PT,
                                             TEXT_ALIGN_LEFT, white, longtext,
                                             sixCols(), 0, INT16_MAX);
    point_t sz_short = gfx_printBufferClipped(start, FONT_SIZE_5PT,
                                              TEXT_ALIGN_LEFT, white, shorttext,
                                              sixCols(), 0, INT16_MAX);

    REQUIRE((sz_long.y - sz_short.y) == (int16_t)(29 * lineH()));
}

/*
 * Regression: the decoder must render the LAST glyph of a font. lv_font_conv's
 * loca table stores exactly glyph_cnt start-offsets with no trailing end
 * sentinel, so the last glyph's end is the glyf table length -- reading
 * loca[glyph_cnt] instead hits table padding (often 0) and silently drops the
 * glyph. The icon font's last glyph is the backspace (U+F55A) and the text
 * font's is the FontAwesome battery (U+F244); a dropped glyph measures 0 wide.
 */
TEST_CASE("last glyph of each font decodes (non-zero width)",
          "[gfx][text][font]")
{
    /* U+F55A backspace: last glyph of the custom icon/fallback font. */
    REQUIRE(gfx_getTextWidth(FONT_SIZE_8PT, "\xEF\x95\x9A") > 0);
    REQUIRE(gfx_getTextWidth(FONT_SIZE_16PT, "\xEF\x95\x9A") > 0);
    /* U+F244 battery: last glyph of the Ubuntu text font's FontAwesome subset. */
    REQUIRE(gfx_getTextWidth(FONT_SIZE_8PT, "\xEF\x89\x84") > 0);
    REQUIRE(gfx_getTextWidth(FONT_SIZE_16PT, "\xEF\x89\x84") > 0);
}

/* Built-in 16 pt text font, embedded by fontData.S. */
extern "C" const uint8_t _font_16_start[];
extern "C" const uint8_t _font_16_end[];

TEST_CASE("gfx_setFontSource replaces a font and restores the built-in one",
          "[gfx][font]")
{
    const uint8_t h5 = gfx_getFontLineHeight(FONT_SIZE_5PT);
    const uint8_t h16 = gfx_getFontLineHeight(FONT_SIZE_16PT);
    const uint32_t len = (uint32_t)(_font_16_end - _font_16_start);
    REQUIRE(h5 != h16);

    SECTION("a valid font is used for that size")
    {
        REQUIRE(gfx_setFontSource(FONT_SET_TEXT, FONT_SIZE_5PT, _font_16_start,
                                  len));
        REQUIRE(gfx_getFontLineHeight(FONT_SIZE_5PT) == h16);
    }

    SECTION("invalid data is rejected and the font is unchanged")
    {
        static const uint8_t junk[64] = { 0 };
        REQUIRE_FALSE(gfx_setFontSource(FONT_SET_TEXT, FONT_SIZE_5PT, junk,
                                        sizeof(junk)));
        REQUIRE(gfx_getFontLineHeight(FONT_SIZE_5PT) == h5);
    }

    REQUIRE(gfx_setFontSource(FONT_SET_TEXT, FONT_SIZE_5PT, NULL, 0));
    REQUIRE(gfx_getFontLineHeight(FONT_SIZE_5PT) == h5);
}

/* -----------------------------------------------------------------------
 * Built-in font limits.
 * ----------------------------------------------------------------------- */

/**
 * Get the largest glyph box area of a font blob.
 *
 * @param path: font blob path.
 * @return the area in pixels, or 0 if the blob cannot be read.
 */
static uint32_t maxGlyphArea(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == nullptr)
        return 0;
    std::vector<uint8_t> data;
    uint8_t buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        data.insert(data.end(), buf, buf + n);
    fclose(fp);

    lvFont_t font;
    if (!lvFont_init(&font, data.data(), (uint32_t)data.size()))
        return 0;

    uint32_t area = 0;
    for (uint32_t cp = 0; cp < 0x110000; cp++) {
        lvGlyph_t g;
        if (!lvFont_getGlyph(&font, cp, &g))
            continue;
        const uint32_t a = (uint32_t)g.box_w * g.box_h;
        if (a > area)
            area = a;
    }
    return area;
}

TEST_CASE("Every built-in glyph fits the coverage buffer", "[gfx][text]")
{
    static const char *const NAMES[] = { "ubuntu", "icons" };
    static const unsigned SIZES[] = { 5, 6, 8, 9, 10, 12, 16 };
    static const unsigned BPPS[] = { 1, 4 };

    uint32_t largest = 0;
    for (const char *name : NAMES) {
        for (unsigned pt : SIZES) {
            for (unsigned bpp : BPPS) {
                char path[256];
                snprintf(path, sizeof(path), "%s/%s_%u_%u.bin", FONT_DIR, name,
                         pt, bpp);
                INFO(path);
                const uint32_t area = maxGlyphArea(path);
                REQUIRE(area > 0);
                REQUIRE(area <= FONT_MAX_GLYPH_AREA);
                if (area > largest)
                    largest = area;
            }
        }
    }

    /* The buffer is no larger than it needs to be. */
    REQUIRE(largest == FONT_MAX_GLYPH_AREA);
}
