/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_DRAWCTX_HPP
#define ORTX_UI_DRAWCTX_HPP

#include <array>
#include "core/graphics.h"
#include "render/Geometry.hpp"
#include "style/SemanticColor.hpp"

namespace ortxui
{

/**
 * Drawing surface over the gfx* backend: colours are semantic roles resolved
 * through the active theme, and drawing is clipped to the part of the screen
 * being painted (the whole screen, or one band).
 */
class DrawCtx
{
public:
    /** Destructor. */
    ~DrawCtx() = default;

    /**
     * Constructor, drawing on part of the screen.
     *
     * @param bounds: the part of the screen being drawn, the outermost clip.
     */
    explicit DrawCtx(const Rect &bounds);

    /** Current clip rectangle, in screen space. */
    const Rect &clip() const
    {
        return clipRect;
    }

    /**
     * Narrow the clip to its intersection with a rectangle, saving the
     * previous clip for popClip().
     *
     * @param r: rectangle to clip to.
     */
    void pushClip(const Rect &r);

    /** Restore the clip saved by the matching pushClip(). */
    void popClip();

    /**
     * Fill the whole screen.
     *
     * @param bg: colour role.
     */
    void clearScreen(Sem bg);

    /**
     * Draw a filled rectangle.
     *
     * @param r: rectangle.
     * @param color: colour role.
     */
    void fillRect(const Rect &r, Sem color);

    /**
     * Draw a filled rectangle with anti-aliased rounded corners.
     *
     * @param r: rectangle.
     * @param radius: corner radius, clamped to half the smaller side.
     * @param color: colour role.
     */
    void fillRoundRect(const Rect &r, uint16_t radius, Sem color);

    /**
     * Draw a 1px rectangle outline.
     *
     * @param r: rectangle.
     * @param color: colour role.
     */
    void drawRect(const Rect &r, Sem color);

    /**
     * Draw an anti-aliased 1px circle outline.
     *
     * @param c: centre.
     * @param r: radius.
     * @param color: colour role.
     */
    void drawCircle(Point c, uint16_t r, Sem color);

    /**
     * Draw an anti-aliased filled disc.
     *
     * @param c: centre.
     * @param r: radius.
     * @param color: colour role.
     */
    void fillCircle(Point c, uint16_t r, Sem color);

    /**
     * Draw a 1px line.
     *
     * @param a: first end.
     * @param b: second end.
     * @param color: colour role.
     */
    void line(Point a, Point b, Sem color);

    /**
     * Draw an anti-aliased line; 1bpp targets get a sharp line.
     *
     * @param a: first end.
     * @param b: second end.
     * @param color: colour role.
     * @param width: stroke width, in pixels.
     */
    void lineAA(Point a, Point b, Sem color, float width = LINE_AA_WIDTH);

    /**
     * Draw text, which may span several lines.
     *
     * @param at: start of the first baseline.
     * @param size: font size.
     * @param align: alignment, against the whole screen.
     * @param color: colour role.
     * @param str: NUL-terminated text.
     * @return the extent of the drawn text.
     */
    point_t text(Point at, fontSize_t size, textAlign_t align, Sem color,
                 const char *str);

    /**
     * Draw one line of text aligned in a box and centred vertically.
     *
     * @param box: box to align in.
     * @param size: font size.
     * @param align: horizontal alignment within the box.
     * @param color: colour role.
     * @param str: NUL-terminated text.
     */
    void textInBox(const Rect &box, fontSize_t size, textAlign_t align,
                   Sem color, const char *str);

    /**
     * Draw one line of text in a box as textInBox() does, cut and ended with
     * an ellipsis when it is wider than the box.
     *
     * @param box: box to align in.
     * @param size: font size.
     * @param align: horizontal alignment within the box.
     * @param color: colour role.
     * @param str: NUL-terminated text.
     */
    void textInBoxEllipsized(const Rect &box, fontSize_t size,
                             textAlign_t align, Sem color, const char *str);

    /**
     * Draw left-aligned text wrapped as gfx_measureText() wraps it, painting
     * only the rows in a range.
     *
     * @param at: start of the first baseline.
     * @param size: font size.
     * @param color: colour role.
     * @param str: NUL-terminated text.
     * @param maxX: column to wrap at, exclusive.
     * @param clipTop: first row to paint.
     * @param clipBot: last row to paint, inclusive.
     */
    void textWrapped(Point at, fontSize_t size, Sem color, const char *str,
                     int16_t maxX, int16_t clipTop, int16_t clipBot);

private:
    static constexpr uint8_t CLIP_STACK_MAX = 16; ///< Deepest nesting of clips
    static constexpr float LINE_AA_WIDTH = 1.3f;  ///< Default lineAA() width

    std::array<Rect, CLIP_STACK_MAX> clipStack;   ///< Clips saved by pushClip()
    Rect clipRect;                                ///< Current clip
    uint8_t clipDepth = 0;                        ///< Clips on clipStack
};

} // namespace ortxui

#endif /* ORTX_UI_DRAWCTX_HPP */
