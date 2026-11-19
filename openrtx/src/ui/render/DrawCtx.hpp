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
 * Thin, theme-aware drawing surface layered over the gfx* backend.
 *
 * Every colour is supplied as a semantic role and resolved through the active
 * Theme, so drawing code never mentions raw colours. The context also carries
 * a clip rectangle, starting from the part of the screen being drawn: the
 * whole screen, or one band on targets drawing in bands. Painting skips the
 * objects outside it.
 *
 * The gfx* backend owns the static framebuffer or band buffer; the toolkit
 * never allocates one. The UI loop sends what was drawn to the panel.
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

    /** Current clip rectangle (screen space). */
    const Rect &clip() const
    {
        return clipRect;
    }

    /** Narrow the clip to the intersection of the current clip and `r`, and
     *  push the previous clip so popClip() restores it. Drawing outside the
     *  active clip is suppressed by the backend. */
    void pushClip(const Rect &r);

    /** Restore the clip saved by the matching pushClip(). */
    void popClip();

    /** Fill the whole screen with a role colour. */
    void clearScreen(Sem bg);

    /** Filled rectangle. */
    void fillRect(const Rect &r, Sem color);

    /** Filled rounded rectangle; the corners are anti-aliased (arcs of the AA
     *  fillCircle), the straight edges crisp. `radius` is clamped to half the
     *  smaller side. */
    void fillRoundRect(const Rect &r, uint16_t radius, Sem color);

    /** 1px rectangle outline. */
    void drawRect(const Rect &r, Sem color);

    /** 1px circle outline centred at `c`. */
    void drawCircle(Point c, uint16_t r, Sem color);

    /** Anti-aliased filled disc centred at `c`, shaded per pixel by coverage. */
    void fillCircle(Point c, uint16_t r, Sem color);

    /** 1px line between two points. */
    void line(Point a, Point b, Sem color);

    /** Anti-aliased line of the given width (coverage-blended; a crisp 1px line
     *  on 1bpp targets since gfx_setPixel thresholds there). */
    void lineAA(Point a, Point b, Sem color, float width = LINE_AA_WIDTH);

    /**
     * Draw a NUL-terminated string. Returns the drawn text extent as reported
     * by the backend. Multi-line strings (with '\n') are supported.
     */
    point_t text(Point at, fontSize_t size, textAlign_t align, Sem color,
                 const char *str);

    /**
     * Draw a single line of text aligned within a box. Unlike the raw backend,
     * whose CENTER/RIGHT resolve against the whole screen, this measures the
     * text and places it relative to `box`: horizontally per `align`, and
     * vertically centred. This is what lets layout-assigned areas drive text.
     */
    void textInBox(const Rect &box, fontSize_t size, textAlign_t align,
                   Sem color, const char *str);

    /**
     * Like textInBox(), but if the text is wider than the box it is truncated
     * and an ellipsis ("...") is appended so it fits. Used for list values and
     * labels that may hold arbitrary-length content.
     */
    void textInBoxEllipsized(const Rect &box, fontSize_t size,
                             textAlign_t align, Sem color, const char *str);

    /**
     * Draw left-aligned text wrapped at `maxX` (exclusive), its first baseline
     * at `at`, painting only the rows from `clipTop` to `clipBot` inclusive.
     * Wraps as gfx_measureText() measures, so the two agree on the height.
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
