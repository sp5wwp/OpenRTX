/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>

extern "C" {
#include "core/graphics.h"
#include "interfaces/display.h"
}

/*
 * Drawing in bands must give the pixels drawing in a framebuffer gives. This
 * test is built both ways, drawing in 16-row bands and in a framebuffer, and
 * checks the same expectations in both; the display is stubbed to keep what
 * it is sent.
 */

static constexpr int16_t W = CONFIG_SCREEN_WIDTH;  ///< Screen width
static constexpr int16_t H = CONFIG_SCREEN_HEIGHT; ///< Screen height

/** Fill of the rows the display was not sent, to catch them. */
static constexpr uint16_t UNSENT = 0xAAAA;

static constexpr color_t RED = { 255, 0, 0, 255 };       ///< Opaque red
static constexpr color_t BLUE = { 0, 0, 255, 255 };      ///< Opaque blue
static constexpr color_t GREY = { 90, 90, 90, 255 };     ///< Opaque grey
static constexpr color_t WHITE = { 255, 255, 255, 255 }; ///< Opaque white

/** Translucent green, blended with what is under it. */
static constexpr color_t SEE_THROUGH_GREEN = { 0, 255, 0, 100 };

/** Translucent yellow, blended with what is under it. */
static constexpr color_t SEE_THROUGH_YELLOW = { 255, 230, 0, 180 };

/** The screen as the display was sent it. */
static uint16_t screen[H * W];

/** Display stub: nothing to set up. */
void display_init()
{
}

/** Display stub: nothing to shut down. */
void display_terminate()
{
}

/** Display stub: keep rows of a framebuffer, see display.h. */
void display_renderRows(uint8_t startRow, uint8_t endRow, void *fb)
{
    const uint16_t *rows = static_cast<const uint16_t *>(fb) + (startRow * W);
    memcpy(&screen[startRow * W], rows,
           (endRow - startRow) * W * sizeof(uint16_t));
}

/** Display stub: keep a whole framebuffer, see display.h. */
void display_render(void *fb)
{
    display_renderRows(0, H, fb);
}

/** Display stub: keep the rows of a band, see display.h. */
void display_renderBand(uint16_t startRow, uint16_t endRow, void *band)
{
    memcpy(&screen[startRow * W], band,
           (endRow - startRow) * W * sizeof(uint16_t));
}

/** Display stub: no contrast to set. */
void display_setContrast(uint8_t contrast)
{
    (void)contrast;
}

/** Display stub: no backlight to set. */
void display_setBacklightLevel(uint8_t level)
{
    (void)level;
}

/**
 * Draw the whole screen and send it, band by band as the UI does.
 *
 * @param scene: draws the screen content.
 */
template <typename Scene>
static void drawScreen(Scene scene)
{
    for (size_t i = 0; i < (size_t)(H * W); i++)
        screen[i] = UNSENT;

    for (uint16_t y = 0; y < H; y += GFX_BAND_ROWS) {
        gfx_beginBand(y);
        gfx_resetClipRect();
        gfx_clearScreen();
        scene();
        gfx_render();
    }
}

/**
 * Get a pixel the display was sent.
 *
 * @param x: column.
 * @param y: row.
 * @return the RGB565 pixel.
 */
static uint16_t pixel(int16_t x, int16_t y)
{
    return screen[(y * W) + x];
}

/**
 * Convert a colour to RGB565, as gfx_setPixel stores an opaque one.
 *
 * @param c: colour.
 * @return the RGB565 pixel.
 */
static uint16_t rgb565(color_t c)
{
    return static_cast<uint16_t>(((c.r >> 3) << 11) | ((c.g >> 2) << 5)
                                 | (c.b >> 3));
}

/**
 * Blend one channel of a colour over the same channel of a pixel.
 *
 * @param under: pixel blended over.
 * @param over: pixel of the colour.
 * @param alpha: opacity of the colour.
 * @param shift: position of the channel.
 * @param mask: mask of the channel.
 * @return the blended channel, in place.
 */
static unsigned blendChannel(uint16_t under, uint16_t over, unsigned alpha,
                             unsigned shift, unsigned mask)
{
    const unsigned u = (under >> shift) & mask;
    const unsigned o = (over >> shift) & mask;
    return (((255 - alpha) * u + alpha * o) / 255) << shift;
}

/**
 * Blend a colour over a pixel, as gfx_setPixel does with a translucent one.
 *
 * @param under: RGB565 pixel blended over.
 * @param c: colour.
 * @return the RGB565 pixel.
 */
static uint16_t blend(uint16_t under, color_t c)
{
    const uint16_t over = rgb565(c);
    return static_cast<uint16_t>(blendChannel(under, over, c.alpha, 11, 0x1f)
                                 | blendChannel(under, over, c.alpha, 5, 0x3f)
                                 | blendChannel(under, over, c.alpha, 0, 0x1f));
}

/**
 * Tell whether a pixel is inside a rectangle.
 *
 * @param x, y: pixel.
 * @param x0, y0: top left corner of the rectangle.
 * @param w, h: size of the rectangle.
 * @return true when inside.
 */
static bool inside(int16_t x, int16_t y, int16_t x0, int16_t y0, int16_t w,
                   int16_t h)
{
    return (x >= x0) && (x < x0 + w) && (y >= y0) && (y < y0 + h);
}

/**
 * Draw a small scene of every kind of primitive, within 16 rows from its top.
 *
 * @param top: first row of the scene.
 */
static void drawSampleScene(int16_t top)
{
    gfx_drawRect({ 0, top }, W, 16, GREY, true);
    gfx_print({ 2, static_cast<int16_t>(top + 12) }, FONT_SIZE_8PT,
              TEXT_ALIGN_LEFT, SEE_THROUGH_YELLOW, "Band 123");
    gfx_drawLine({ 70, static_cast<int16_t>(top + 1) },
                 { 150, static_cast<int16_t>(top + 14) }, WHITE);
    gfx_drawCircle({ 120, static_cast<int16_t>(top + 8) }, 6, RED);
    gfx_drawRect({ 60, static_cast<int16_t>(top + 2) }, 20, 12,
                 SEE_THROUGH_GREEN, false);
    gfx_printBufferClipped({ 82, static_cast<int16_t>(top + 12) },
                           FONT_SIZE_10PT, TEXT_ALIGN_LEFT, WHITE, "Wg", W,
                           static_cast<int16_t>(top + 4),
                           static_cast<int16_t>(top + 10));
}

TEST_CASE("Filled rectangles cover every band they cross", "[gfx][band]")
{
    SECTION("rectangle across two band edges")
    {
        drawScreen([] { gfx_drawRect({ 10, 5 }, 50, 40, RED, true); });

        for (int16_t y = 0; y < H; y++) {
            for (int16_t x = 0; x < W; x++) {
                const bool in = inside(x, y, 10, 5, 50, 40);
                REQUIRE(pixel(x, y) == (in ? rgb565(RED) : 0));
            }
        }
    }

    SECTION("rectangle running past the bottom of the screen")
    {
        drawScreen([] { gfx_drawRect({ 0, 100 }, W, 60, RED, true); });

        for (int16_t y = 0; y < H; y++) {
            const uint16_t want = (y >= 100) ? rgb565(RED) : 0;
            REQUIRE(pixel(0, y) == want);
            REQUIRE(pixel(W - 1, y) == want);
        }
    }

    SECTION("rectangle starting left of and above the screen")
    {
        drawScreen([] { gfx_drawRect({ -10, -6 }, 40, 30, RED, true); });

        for (int16_t y = 0; y < H; y++) {
            for (int16_t x = 0; x < W; x++) {
                const bool in = inside(x, y, -10, -6, 40, 30);
                REQUIRE(pixel(x, y) == (in ? rgb565(RED) : 0));
            }
        }
    }

    SECTION("rectangles wholly off the screen")
    {
        drawScreen([] {
            gfx_drawRect({ -60, 10 }, 50, 20, RED, true);
            gfx_drawRect({ 10, -40 }, 50, 20, RED, true);
            gfx_drawRect({ W, 10 }, 50, 20, RED, true);
            gfx_drawRect({ 10, H }, 50, 20, RED, true);
        });

        for (int16_t y = 0; y < H; y++)
            for (int16_t x = 0; x < W; x++)
                REQUIRE(pixel(x, y) == 0);
    }

    SECTION("rectangle outline across a band edge")
    {
        drawScreen([] { gfx_drawRect({ 20, 10 }, 30, 20, RED, false); });

        for (int16_t y = 0; y < H; y++) {
            for (int16_t x = 0; x < W; x++) {
                const bool edge = inside(x, y, 20, 10, 30, 20)
                               && !inside(x, y, 21, 11, 28, 18);
                REQUIRE(pixel(x, y) == (edge ? rgb565(RED) : 0));
            }
        }
    }
}

TEST_CASE("Alpha blending across band edges blends over what is below",
          "[gfx][band]")
{
    drawScreen([] {
        gfx_drawRect({ 0, 0 }, W, 64, BLUE, true);
        gfx_drawRect({ 20, 10 }, 30, 30, SEE_THROUGH_GREEN, true);
    });

    const uint16_t blended = blend(rgb565(BLUE), SEE_THROUGH_GREEN);
    REQUIRE(blended != rgb565(BLUE));
    for (int16_t y = 0; y < H; y++) {
        for (int16_t x = 0; x < W; x++) {
            uint16_t want = 0;
            if (inside(x, y, 20, 10, 30, 30))
                want = blended;
            else if (y < 64)
                want = rgb565(BLUE);
            REQUIRE(pixel(x, y) == want);
        }
    }
}

TEST_CASE("The clip rectangle holds in every band", "[gfx][band]")
{
    SECTION("clip across a band edge")
    {
        drawScreen([] {
            gfx_setClipRect(30, 12, 40, 30);
            gfx_fillScreen(WHITE);
        });

        for (int16_t y = 0; y < H; y++) {
            for (int16_t x = 0; x < W; x++) {
                const bool in = inside(x, y, 30, 12, 40, 30);
                REQUIRE(pixel(x, y) == (in ? rgb565(WHITE) : 0));
            }
        }
    }

    SECTION("clip larger than the screen")
    {
        drawScreen([] {
            gfx_setClipRect(-10, 100, 500, 100);
            gfx_fillScreen(WHITE);
        });

        for (int16_t y = 0; y < H; y++) {
            const uint16_t want = (y >= 100) ? rgb565(WHITE) : 0;
            REQUIRE(pixel(0, y) == want);
            REQUIRE(pixel(W - 1, y) == want);
        }
    }

    SECTION("clip removed")
    {
        drawScreen([] {
            gfx_setClipRect(30, 12, 40, 30);
            gfx_resetClipRect();
            gfx_fillScreen(WHITE);
        });

        for (int16_t y = 0; y < H; y++)
            REQUIRE(pixel(5, y) == rgb565(WHITE));
    }
}

TEST_CASE("Drawing across a band edge matches drawing inside a band",
          "[gfx][band]")
{
    static uint16_t inBand[16 * W];

    drawScreen([] { drawSampleScene(0); });
    memcpy(inBand, screen, sizeof(inBand));

    /* The scene draws more than its background. */
    bool drawn = false;
    for (int16_t x = 0; x < W; x++)
        drawn = drawn || (inBand[(12 * W) + x] != rgb565(GREY));
    REQUIRE(drawn);

    for (int16_t top : { 3, 8, 13, 40 }) {
        drawScreen([top] { drawSampleScene(top); });

        for (int16_t y = 0; y < 16; y++) {
            for (int16_t x = 0; x < W; x++)
                REQUIRE(pixel(x, static_cast<int16_t>(top + y))
                        == inBand[(y * W) + x]);
        }
    }
}
