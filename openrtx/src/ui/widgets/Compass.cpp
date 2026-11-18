/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/Compass.hpp"
#include "render/DrawCtx.hpp"
#include "core/graphics.h"

#include <cmath>
#include <cstdio>

namespace ortxui
{

void Compass::setHeading(int16_t deg, bool valid)
{
    /* Normalise into 0-359 so the needle maths and label agree. */
    int16_t d = static_cast<int16_t>(deg % 360);
    if (d < 0)
        d = static_cast<int16_t>(d + 360);
    heading_ = d;
    valid_ = valid;

    /* The bitmap font is ASCII-only (no degree glyph), so the heading reads as
     * a plain 3-digit bearing; the dial supplies the "degrees" context. */
    if (valid)
        snprintf(label_, sizeof(label_), "%03d", d);
    else
        snprintf(label_, sizeof(label_), "---");
}

void Compass::draw(DrawCtx &d)
{
    /* Reserve a strip at the bottom for the heading readout; the dial fills the
     * square space above it. */
    const int16_t labelH = 12;
    const int16_t dialH = static_cast<int16_t>(area_.h - labelH);
    const int16_t side = (area_.w < dialH) ? area_.w : dialH;
    const int16_t r =
        static_cast<int16_t>(side / 2 - 8); /* room for cardinals */
    const Point c = { static_cast<int16_t>(area_.x + area_.w / 2),
                      static_cast<int16_t>(area_.y + dialH / 2) };

    if (r <= 4)
        return;

    d.drawCircle(c, static_cast<uint16_t>(r), Sem::OnSurfaceMuted);

    /* Cardinal letters just outside the ring (N top, E right, S bottom, W
     * left), each centred in a small box straddling the ring. */
    const int16_t o = static_cast<int16_t>(r + 6);
    struct {
        const char *s;
        int16_t dx, dy;
    } card[4] = { { "N", 0, static_cast<int16_t>(-o) },
                  { "E", o, 0 },
                  { "S", 0, o },
                  { "W", static_cast<int16_t>(-o), 0 } };
    for (auto &k : card) {
        const Rect box = { static_cast<int16_t>(c.x + k.dx - 5),
                           static_cast<int16_t>(c.y + k.dy - 5), 10, 10 };
        d.textInBox(box, FONT_SIZE_6PT, TEXT_ALIGN_CENTER, Sem::OnSurfaceMuted,
                    k.s);
    }

    /* Heading readout under the dial. */
    const Rect lblBox = { area_.x, static_cast<int16_t>(area_.y + dialH),
                          area_.w, static_cast<uint16_t>(labelH) };
    d.textInBox(lblBox, FONT_SIZE_6PT, TEXT_ALIGN_CENTER, Sem::OnSurface,
                label_);

    if (!valid_)
        return;

    /* Needle: unit vector toward the heading (0 = up/north, clockwise). */
    const float rad = static_cast<float>(heading_) * 3.14159265f / 180.0f;
    const float ux = sinf(rad);
    const float uy = -cosf(rad);
    const float px = -uy; /* perpendicular for the arrowhead */
    const float py = ux;

    const int16_t tipLen = static_cast<int16_t>(r - 2);
    const int16_t tailLen = static_cast<int16_t>(r - 6);
    const Point tip = { static_cast<int16_t>(c.x + ux * tipLen),
                        static_cast<int16_t>(c.y + uy * tipLen) };
    const Point tail = { static_cast<int16_t>(c.x - ux * tailLen),
                         static_cast<int16_t>(c.y - uy * tailLen) };
    d.line(tail, tip, Sem::TxDanger);

    /* Two short strokes from the tip make a filled-looking arrowhead. */
    const int16_t ab = static_cast<int16_t>(tipLen - 6);
    const Point base = { static_cast<int16_t>(c.x + ux * ab),
                         static_cast<int16_t>(c.y + uy * ab) };
    const Point a1 = { static_cast<int16_t>(base.x + px * 4),
                       static_cast<int16_t>(base.y + py * 4) };
    const Point a2 = { static_cast<int16_t>(base.x - px * 4),
                       static_cast<int16_t>(base.y - py * 4) };
    d.line(tip, a1, Sem::TxDanger);
    d.line(tip, a2, Sem::TxDanger);

    d.fillCircle(c, 2, Sem::OnSurface);
}

} // namespace ortxui
