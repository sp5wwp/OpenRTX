/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "render/DrawCtx.hpp"
#include "style/Theme.hpp"
#include "style/Symbols.hpp"
#include "hwconfig.h"

#include <cmath>
#include <cstring>

namespace ortxui
{

DrawCtx::DrawCtx() : clip_{ 0, 0, CONFIG_SCREEN_WIDTH, CONFIG_SCREEN_HEIGHT }
{
    gfx_resetClipRect();
}

void DrawCtx::pushClip(const Rect &r)
{
    if (clipDepth_ < kClipStackMax)
        clipStack_[clipDepth_] = clip_;
    if (clipDepth_ < 0xFFu)
        clipDepth_++;
    clip_ = intersect(clip_, r);
    gfx_setClipRect(clip_.x, clip_.y, clip_.w, clip_.h);
}

void DrawCtx::popClip()
{
    if (clipDepth_ > 0)
        clipDepth_--;
    clip_ = (clipDepth_ < kClipStackMax) ?
                clipStack_[clipDepth_] :
                Rect{ 0, 0, CONFIG_SCREEN_WIDTH, CONFIG_SCREEN_HEIGHT };
    gfx_setClipRect(clip_.x, clip_.y, clip_.w, clip_.h);
}

void DrawCtx::clearScreen(Sem bg)
{
    gfx_fillScreen(themeColor(bg));
}

void DrawCtx::fillRect(const Rect &r, Sem color)
{
    if (r.empty())
        return;

    const point_t start = { r.x, r.y };
    gfx_drawRect(start, r.w, r.h, themeColor(color), true);
}

void DrawCtx::fillRoundRect(const Rect &box, uint16_t radius, Sem color)
{
    if (box.empty())
        return;

    uint16_t rr = radius;
    if (rr > box.w / 2)
        rr = box.w / 2;
    if (rr > box.h / 2)
        rr = box.h / 2;
    if (rr == 0) {
        fillRect(box, color);
        return;
    }

    const int16_t x0 = box.x;
    const int16_t y0 = box.y;
    const int16_t x1 = static_cast<int16_t>(box.x + box.w - 1);
    const int16_t y1 = static_cast<int16_t>(box.y + box.h - 1);

    /* Middle band (full height) plus the two side bands between the corners. */
    fillRect({ static_cast<int16_t>(x0 + rr), y0,
               static_cast<uint16_t>(box.w - 2 * rr), box.h },
             color);
    fillRect({ x0, static_cast<int16_t>(y0 + rr), rr,
               static_cast<uint16_t>(box.h - 2 * rr) },
             color);
    fillRect({ static_cast<int16_t>(x1 - rr + 1), static_cast<int16_t>(y0 + rr),
               rr, static_cast<uint16_t>(box.h - 2 * rr) },
             color);

    /* Anti-aliased corners: a fillCircle at each corner centre rounds it. */
    const int16_t cl = static_cast<int16_t>(x0 + rr);
    const int16_t cr = static_cast<int16_t>(x1 - rr);
    const int16_t ct = static_cast<int16_t>(y0 + rr);
    const int16_t cb = static_cast<int16_t>(y1 - rr);
    fillCircle({ cl, ct }, rr, color);
    fillCircle({ cr, ct }, rr, color);
    fillCircle({ cl, cb }, rr, color);
    fillCircle({ cr, cb }, rr, color);
}

void DrawCtx::drawRect(const Rect &r, Sem color)
{
    if (r.empty())
        return;

    const point_t start = { r.x, r.y };
    gfx_drawRect(start, r.w, r.h, themeColor(color), false);
}

void DrawCtx::drawCircle(Point c, uint16_t r, Sem color)
{
    /* Anti-aliased ~1px ring: coverage peaks on the radius and fades either
     * side; gfx_setPixel blends the modulated alpha (thresholded on mono). */
    const color_t col = themeColor(color);
    const float rf = static_cast<float>(r);
    const int R = static_cast<int>(r) + 1;
    for (int dy = -R; dy <= R; dy++) {
        for (int dx = -R; dx <= R; dx++) {
            const float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
            float cov = 1.0f - std::fabs(dist - rf);
            if (cov <= 0.0f)
                continue;
            if (cov > 1.0f)
                cov = 1.0f;
            color_t p = col;
            p.alpha = static_cast<uint8_t>(col.alpha * cov);
            gfx_setPixel({ static_cast<int16_t>(c.x + dx),
                           static_cast<int16_t>(c.y + dy) },
                         p);
        }
    }
}

void DrawCtx::fillCircle(Point c, uint16_t r, Sem color)
{
    /* Anti-aliased disc: full coverage inside with a 1px soft edge. */
    const color_t col = themeColor(color);
    const float rf = static_cast<float>(r);
    const int R = static_cast<int>(r) + 1;
    for (int dy = -R; dy <= R; dy++) {
        for (int dx = -R; dx <= R; dx++) {
            const float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
            float cov = rf + 0.5f - dist;
            if (cov <= 0.0f)
                continue;
            if (cov > 1.0f)
                cov = 1.0f;
            color_t p = col;
            p.alpha = static_cast<uint8_t>(col.alpha * cov);
            gfx_setPixel({ static_cast<int16_t>(c.x + dx),
                           static_cast<int16_t>(c.y + dy) },
                         p);
        }
    }
}

void DrawCtx::line(Point a, Point b, Sem color)
{
    const point_t start = { a.x, a.y };
    const point_t end = { b.x, b.y };
    gfx_drawLine(start, end, themeColor(color));
}

void DrawCtx::lineAA(Point a, Point b, Sem color, float width)
{
    const color_t col = themeColor(color);
    const float x0 = a.x, y0 = a.y, x1 = b.x, y1 = b.y;
    const float dx = x1 - x0, dy = y1 - y0;
    const float len2 = dx * dx + dy * dy;
    const float half = width * 0.5f;

    /* Rasterise the segment's bounding box (padded), shading each pixel by its
     * coverage = how far its centre is inside the stroke's half-width. */
    const int minx =
        static_cast<int>(std::floor(std::fmin(x0, x1) - half - 1.0f));
    const int maxx =
        static_cast<int>(std::ceil(std::fmax(x0, x1) + half + 1.0f));
    const int miny =
        static_cast<int>(std::floor(std::fmin(y0, y1) - half - 1.0f));
    const int maxy =
        static_cast<int>(std::ceil(std::fmax(y0, y1) + half + 1.0f));

    for (int py = miny; py <= maxy; py++) {
        for (int px = minx; px <= maxx; px++) {
            /* Distance from the pixel centre to the segment. */
            const float fx = px - x0, fy = py - y0;
            float t = (len2 > 0.0f) ? ((fx * dx + fy * dy) / len2) : 0.0f;
            if (t < 0.0f)
                t = 0.0f;
            if (t > 1.0f)
                t = 1.0f;
            const float cx = x0 + t * dx, cy = y0 + t * dy;
            const float dist =
                std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
            float cov = half + 0.5f - dist;
            if (cov <= 0.0f)
                continue;
            if (cov > 1.0f)
                cov = 1.0f;
            color_t p = col;
            p.alpha = static_cast<uint8_t>(col.alpha * cov);
            gfx_setPixel({ static_cast<int16_t>(px), static_cast<int16_t>(py) },
                         p);
        }
    }
}

point_t DrawCtx::text(Point at, fontSize_t size, textAlign_t align, Sem color,
                      const char *str)
{
    const point_t start = { at.x, at.y };
    /* "%s" keeps arbitrary content (including '%') safe through the backend's
     * printf-style formatter. */
    return gfx_print(start, size, align, themeColor(color), "%s", str);
}

void DrawCtx::textInBox(const Rect &box, fontSize_t size, textAlign_t align,
                        Sem color, const char *str)
{
    const int tw = static_cast<int>(gfx_getTextWidth(size, str));
    /* True metric ascent (baseline -> top of the tallest glyph), NOT
     * gfx_getFontHeight, which is the '|'-glyph box height (a cap-height proxy
     * that runs a couple of pixels past the baseline) -- using that here would
     * push the baseline down and clip the tallest glyphs (e.g. the '[' ']' of
     * the M17 destination cursor). */
    const int ascent = static_cast<int>(gfx_getFontAscent(size));
    const int lineH = static_cast<int>(gfx_getFontLineHeight(size));

    int16_t x = box.x;
    if (align == TEXT_ALIGN_CENTER)
        x = static_cast<int16_t>(box.x + (static_cast<int>(box.w) - tw) / 2);
    else if (align == TEXT_ALIGN_RIGHT)
        x = static_cast<int16_t>(box.x + static_cast<int>(box.w) - tw);

    /* The backend anchors a line by its baseline. Centre the whole glyph box
     * (ascent above the baseline, descent below) in the frame, so descenders
     * have room below the baseline instead of the baseline sitting on the
     * frame's bottom edge and clipping them. */
    const int16_t baseline = static_cast<int16_t>(
        box.y + (static_cast<int>(box.h) - lineH) / 2 + ascent);
    const point_t start = { x, baseline };
    gfx_print(start, size, TEXT_ALIGN_LEFT, themeColor(color), "%s", str);
}

void DrawCtx::textInBoxEllipsized(const Rect &box, fontSize_t size,
                                  textAlign_t align, Sem color, const char *str)
{
    if (str == nullptr)
        return;
    if (gfx_getTextWidth(size, str) <= box.w) {
        textInBox(box, size, align, color, str);
        return;
    }

    /* Too wide: drop trailing characters until the prefix plus the ellipsis
     * glyph (U+2026, from the fallback font) fits. */
    static const char kEllipsis[] = SYMBOL_ELLIPSIS;
    const uint16_t ellW = gfx_getTextWidth(size, kEllipsis);

    char buf[64];
    size_t n = strnlen(str, sizeof(buf) - sizeof(kEllipsis));
    memcpy(buf, str, n);
    buf[n] = '\0';
    while ((n > 0)
           && (static_cast<uint16_t>(gfx_getTextWidth(size, buf) + ellW)
               > box.w)) {
        buf[--n] = '\0';
    }
    memcpy(buf + n, kEllipsis, sizeof(kEllipsis)); /* includes the NUL */
    textInBox(box, size, align, color, buf);
}

void DrawCtx::textWrapped(Point at, fontSize_t size, Sem color, const char *str,
                          int16_t maxX, int16_t clipTop, int16_t clipBot)
{
    const point_t start = { at.x, at.y };
    gfx_printBufferClipped(start, size, TEXT_ALIGN_LEFT, themeColor(color), str,
                           static_cast<uint16_t>(maxX), clipTop, clipBot);
}

} // namespace ortxui
