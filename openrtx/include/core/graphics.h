/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hwconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Standard high-level graphic interface for all display types.
 * This interface is based on the lower level interface display.h
 * Many methods of graphics.h are basically calls to methods in display.h.
 *
 * On top of basic framebuffer control, graphics.h implements
 * graphics primitives for drawing fonts and shapes
 *
 *********************** HOW TO MANAGE FRAMEBUFFER *****************************
 *
 * This driver allocates the framebuffer as a block of linearly addressed memory
 * equivalent to an array of CONFIG_SCREEN_HEIGHT*CONFIG_SCREEN_WIDTH elements.
 * With respect to it, screen is indexed in this way:
 *
 *   (0,0)
 *     +-------> x
 *     |
 *     |    o (X,Y)
 *     |
 *     v
 *     y
 *
 * then to set the value of the pixel having coordinates (X,Y), framebuffer has
 * to be indexed in this way: buf[X + Y*CONFIG_SCREEN_WIDTH].
 *
 * Colour targets can instead draw the screen a band of rows at a time, to save
 * the RAM of a full framebuffer: with CONFIG_GFX_BAND_ROWS set, the buffer
 * holds that many rows and the screen is drawn by repeating, for each band,
 * gfx_beginBand(), the drawing of the whole screen, and gfx_render(). Drawing
 * outside the band is clipped away.
 */

#ifndef CONFIG_GFX_BAND_ROWS
#define CONFIG_GFX_BAND_ROWS 0
#endif

/**
 * Rows drawn and sent at a time: the band height on targets drawing in bands,
 * the whole screen on the others.
 */
#if CONFIG_GFX_BAND_ROWS > 0
#define GFX_BAND_ROWS CONFIG_GFX_BAND_ROWS
#else
#define GFX_BAND_ROWS CONFIG_SCREEN_HEIGHT
#endif

/**
 * Structure that represents the X,Y coordinates of a single point
 */
typedef struct point_t {
    int16_t x;
    int16_t y;
} point_t;

/**
 * Structure that represents a single color in the RGB 8 bit per channel format
 */
typedef struct color_t {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t alpha;
} color_t;

typedef enum {
    FONT_SIZE_5PT = 0,
    FONT_SIZE_6PT,
    FONT_SIZE_8PT,
    FONT_SIZE_9PT,
    FONT_SIZE_10PT,
    FONT_SIZE_12PT,
    FONT_SIZE_16PT,

    FONT_SIZE_NUM
} fontSize_t;

typedef enum {
    TEXT_ALIGN_LEFT = 0,
    TEXT_ALIGN_CENTER,
    TEXT_ALIGN_RIGHT
} textAlign_t;

/**
 * This function calls the correspondent method of the low level interface display.h
 * It initializes the display.
 */
void gfx_init();

/**
 * This function calls the correspondent method of the low level interface display.h
 * It terminates the display driver and deallocates the framebuffer.
 */
void gfx_terminate();

/**
 * Start drawing the band of GFX_BAND_ROWS rows from a screen row: until the
 * next band starts, drawing lands in the band and is clipped to its rows, and
 * gfx_render() sends it. Does nothing on targets drawing in a framebuffer.
 *
 * @param startRow: first screen row of the band.
 */
void gfx_beginBand(uint16_t startRow);

/**
 * This function calls the correspondent method of the low level interface display.h
 * Copy a given section, between two given rows, of framebuffer content to the
 * display. On targets drawing in bands, only the rows of the current band are
 * copied.
 * @param startRow: first row of the framebuffer section to be copied
 * @param endRow: last row of the framebuffer section to be copied
 */
void gfx_renderRows(uint16_t startRow, uint16_t endRow);

/**
 * This function calls the correspondent method of the low level interface display.h
 * Copy framebuffer content to the display internal buffer. To be called
 * whenever there is need to update the display. On targets drawing in bands,
 * this sends the current band.
 */
void gfx_render();

/**
 * Clears the content of the screen
 * This results in a black screen on color displays
 * And a white screen on B/W displays
 * On targets drawing in bands, this clears the current band.
 */
void gfx_clearScreen();

/**
 * Fills screen with the specified color, within the clip rectangle and, on
 * targets drawing in bands, the current band.
 * @param color: border and fill color, in color_t format.
 */
void gfx_fillScreen(color_t color);

/**
 * Change the color of a single pixel.
 * @param pos: x,y coordinates of the pixel.
 * @param color: desired pixel color, in color_t format.
 */
void gfx_setPixel(point_t pos, color_t color);

/**
 * Restrict all subsequent drawing to a rectangle (screen space). Pixels outside
 * it are suppressed by gfx_setPixel, so text, fills, lines and circles are all
 * clipped.
 * @param x,y: top-left corner.
 * @param width,height: rectangle size (a zero dimension clips out everything).
 */
void gfx_setClipRect(int16_t x, int16_t y, uint16_t width, uint16_t height);

/**
 * Remove the clip rectangle (drawing is bounded only by the screen again).
 */
void gfx_resetClipRect(void);

/**
 * Select crisp text, drawn with the 1bpp font set, or the default smooth text.
 * On colour targets both sets are embedded and this switches the font returned
 * for all subsequent text. No effect on monochrome targets, whose text is
 * always crisp. Defaults to off (smooth).
 * @param on: true for crisp text, false for smooth text.
 */
void gfx_setFontCrisp(bool on);

/**
 * Font sets: each holds one font per fontSize_t.
 */
enum fontSet {
    FONT_SET_TEXT = 0, ///< Default text font
    FONT_SET_CRISP,    ///< Crisp text font, see gfx_setFontCrisp(); the text
                       ///< font itself on monochrome targets
    FONT_SET_ICONS,    ///< App icon glyphs, fallback of the text fonts
    FONT_SET_NUM       ///< Number of font sets
};

/**
 * Use a font from outside the firmware instead of the built-in one: the hook
 * for installable font packs. The data is borrowed and must stay valid while
 * installed. The first font installed allocates the table of installed fonts
 * on the heap.
 *
 * @param set: font set.
 * @param size: font size.
 * @param data: LVGL binary font, or NULL to restore the built-in font.
 * @param len: data length in bytes.
 * @return false, leaving the font unchanged, if the data is not a valid font
 * or the table cannot be allocated.
 */
bool gfx_setFontSource(enum fontSet set, fontSize_t size, const uint8_t *data,
                       uint32_t len);

/**
 * Draw a line from start to end coordinates, ends included.
 * @param start: line start point, in pixel coordinates.
 * @param end: line end point, in pixel coordinates.
 * @param color: line color, in color_t format.
 */
void gfx_drawLine(point_t start, point_t end, color_t color);

/**
 * Draw a rectangle of specified width, height and color.
 * @param start: screen position of the rectangle, in pixels
 * @param width: rectangle width, in pixels, borders included.
 * @param height: rectangle height, in pixels, borders included.
 * @param color: border and fill color, in color_t format.
 * @param fill: if true the rectangle will be solid, otherwise it will be empty with a 1-pixel border
 */
void gfx_drawRect(point_t start, uint16_t width, uint16_t height, color_t color,
                  bool fill);

/**
 * Draw the outline of a circle of specified radius and color.
 * @param start: screen position of the center of the circle, in pixels
 * @param r: circle radius, in pixels, border included.
 * @param color: border color, in color_t format.
 */
void gfx_drawCircle(point_t start, uint16_t r, color_t color);

/**
 * Estimates the maximum font height by reading the gliph | height
 * @param size: text font size, defined as enum.
 * @return font height
 */
uint8_t gfx_getFontHeight(fontSize_t size);

/**
 * Get the ascent of a font, from the font's own header.
 *
 * @param size: text font size, defined as enum.
 * @return pixels above the baseline.
 */
uint8_t gfx_getFontAscent(fontSize_t size);

/**
 * Get the line height of a font, from the font's own header.
 *
 * @param size: text font size, defined as enum.
 * @return ascent minus descent, in pixels.
 */
uint8_t gfx_getFontLineHeight(fontSize_t size);

/**
 * Prints text on the screen at the specified coordinates.
 * Reads text from a given char buffer
 * @param start: text line start point, in pixel coordinates.
 * @param size: text font size, defined as enum.
 * @param alignment: text alignment type, defined as enum.
 * @param color: text color, in color_t format.
 * @param buf: char buffer
 * @return text width and height as point_t coordinates
 */
point_t gfx_printBuffer(point_t start, fontSize_t size, textAlign_t alignment,
                        color_t color, const char *buf);

/**
 * Measure the pixel height of text as it would be laid out by
 * gfx_printBufferClipped, without drawing anything.
 *
 * Simulates the same word-wrap line-break decisions (wrap at max_x).
 * Alignment is not simulated; only the vertical extent is computed.
 * Wrap decisions always use start_x as the left margin of every line,
 * which matches left-aligned layout.  For TEXT_ALIGN_CENTER or
 * TEXT_ALIGN_RIGHT the actual rendered start.x may be larger than
 * start_x, so the wrap point may differ and the returned height may
 * not exactly match what gfx_printBufferClipped produces.
 * Passing char_count < strlen(buf) lets callers find the y-coordinate
 * of an arbitrary cursor position for scroll-offset calculations.
 *
 * start_x must match the x coordinate that will be passed to
 * gfx_printBufferClipped so that wrap decisions use the same effective
 * line width.
 *
 * @param size: text font size.
 * @param buf: NUL-terminated string.
 * @param start_x: left-edge x position (matches gfx_printBufferClipped start).
 * @param max_x: right-edge pixel limit used for word-wrap.
 * @param char_count: characters to measure; pass SIZE_MAX to measure all.
 * @return y-extent in pixels of the text block; always >= one line height.
 */
uint16_t gfx_measureText(fontSize_t size, const char *buf, uint16_t start_x,
                         uint16_t max_x, size_t char_count);

/**
 * Compute how far a block of text has to be shifted up so that the line
 * holding @p cursor_pos stays visible inside a window @p visible_h pixels
 * tall.  Intended for text fields that are edited in place and may be longer
 * than the box they are drawn in: subtract the returned value from the y
 * coordinate handed to gfx_printBufferClipped and the character being edited
 * is always on screen.
 *
 * Wrap decisions are those of gfx_measureText, so @p start_x and @p max_x
 * must match the values passed to gfx_printBufferClipped and the text has to
 * be drawn with TEXT_ALIGN_LEFT for the two to agree.
 *
 * @param size: text font size.
 * @param buf: NUL-terminated string.
 * @param cursor_pos: index of the character to keep visible.
 * @param start_x: left-edge x position (matches gfx_printBufferClipped start).
 * @param max_x: right-edge pixel limit used for word-wrap.
 * @param visible_h: height in pixels of the visible window.
 * @return pixels to shift the text up by; zero when it already fits.
 */
int16_t gfx_scrollOffsetForCursor(fontSize_t size, const char *buf,
                                  size_t cursor_pos, uint16_t start_x,
                                  uint16_t max_x, int16_t visible_h);

/**
 * Measures the pixel width of a single line of text.
 *
 * @param size: text font size.
 * @param buf: NUL-terminated string.
 * @return width in pixels of the text line.
 */
uint16_t gfx_getTextWidth(fontSize_t size, const char *buf);

/**
 * Prints text from a char buffer into a clipped rectangular region.
 *
 * Like gfx_printBuffer but with an explicit right-edge limit and a
 * vertical clip window.  Word-wrap fires when the next glyph would
 * exceed max_x rather than CONFIG_SCREEN_WIDTH.  Pixels outside the
 * range [clip_top_y, clip_bot_y] are suppressed without affecting
 * layout, so a negative start.y can be used to implement scrolling.
 *
 * The returned text_size.y reflects only the lines processed before
 * clip_bot_y is reached, not the full height of the text block.  Use
 * gfx_measureText to obtain the total height for scroll calculations.
 *
 * @param start: top-left origin for the text block, in pixel coordinates.
 * @param size: text font size.
 * @param alignment: text alignment.
 * @param color: text colour.
 * @param buf: NUL-terminated string to render.
 * @param max_x: right-edge pixel limit (exclusive) for word-wrap.
 * @param clip_top_y: topmost pixel row to draw (inclusive).
 * @param clip_bot_y: bottommost pixel row to draw (inclusive).
 * @return text width and height of the processed portion as point_t.
 */
point_t gfx_printBufferClipped(point_t start, fontSize_t size,
                               textAlign_t alignment, color_t color,
                               const char *buf, uint16_t max_x,
                               int16_t clip_top_y, int16_t clip_bot_y);

/**
 * Prints text on the screen at the specified coordinates.
 * @param start: text line start point, in pixel coordinates.
 * @param size: text font size, defined as enum.
 * @param alignment: text alignment type, defined as enum.
 * @param color: text color, in color_t format.
 * @param fmt: printf style format string
 * @return text width and height as point_t coordinates
 */
point_t gfx_print(point_t start, fontSize_t size, textAlign_t alignment,
                  color_t color, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* GRAPHICS_H */
