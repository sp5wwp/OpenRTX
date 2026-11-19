/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "layout/Flex.hpp"
#include "render/DrawCtx.hpp"

#include <algorithm>

namespace ortxui
{

void Flex::draw(DrawCtx &d)
{
    if (hasBg)
        d.fillRect(bounds, background);
}

int16_t Flex::childBase(const Object &c, const LayoutHints &h) const
{
    if (h.basis >= 0)
        return h.basis;

    const Size nat = c.natural();
    return static_cast<int16_t>((axis == Axis::Row) ? nat.w : nat.h);
}

void Flex::onLayout()
{
    const bool row = (axis == Axis::Row);

    /* Content box, inside padding. */
    const int16_t cx = static_cast<int16_t>(bounds.x + padX);
    const int16_t cy = static_cast<int16_t>(bounds.y + padY);
    const int32_t cw = static_cast<int32_t>(bounds.w) - 2 * padX;
    const int32_t ch = static_cast<int32_t>(bounds.h) - 2 * padY;
    if ((cw <= 0) || (ch <= 0))
        return;

    const int32_t mainAvail = row ? cw : ch;
    const int32_t crossAvail = row ? ch : cw;

    /* First pass: sum fixed main extents (incl. margins) and grow weights. */
    int32_t totalBase = 0;
    int32_t totalGrow = 0;
    int32_t count = 0;
    for (Object *c = firstChild(); c != nullptr; c = c->nextSibling()) {
        if (c->hasFlag(FlagHidden))
            continue;
        const LayoutHints h = c->layout();
        count++;
        totalBase += childBase(*c, h) + 2 * h.margin;
        totalGrow += h.grow;
    }
    if (count == 0)
        return;

    const int32_t gaps = gap * (count - 1);
    const int32_t spare = std::max<int32_t>(mainAvail - totalBase - gaps, 0);

    /* Main-axis start cursor and inter-item spacing. A grow weight takes the
     * spare space, so justify only redistributes when nothing grows. */
    int32_t cursor = row ? cx : cy;
    int32_t spacing = gap;
    if (totalGrow == 0) {
        switch (justify) {
            case Justify::Start:
                break;
            case Justify::Center:
                cursor += spare / 2;
                break;
            case Justify::End:
                cursor += spare;
                break;
            case Justify::SpaceBetween:
                if (count > 1)
                    spacing += spare / (count - 1);
                break;
            case Justify::SpaceAround: {
                const int32_t unit = spare / count;
                cursor += unit / 2;
                spacing += unit;
                break;
            }
        }
    }

    /* Second pass: assign each child an absolute area and recurse. */
    for (Object *c = firstChild(); c != nullptr; c = c->nextSibling()) {
        if (c->hasFlag(FlagHidden))
            continue;

        const LayoutHints h = c->layout();
        int32_t mainSize = childBase(*c, h);
        if ((totalGrow > 0) && (h.grow > 0))
            mainSize += spare * h.grow / totalGrow;

        /* Cross-axis size and offset. */
        const Align a = (h.self == Align::Auto) ? align : h.self;
        const int32_t crossInner = crossAvail - 2 * h.margin;
        const Size nat = c->natural();
        int32_t crossSize = row ? nat.h : nat.w;
        int32_t crossOff = 0;
        if (a == Align::Stretch) {
            crossSize = crossInner;
        } else {
            crossSize = std::min(crossSize, crossInner);
            if (a == Align::Center)
                crossOff = (crossInner - crossSize) / 2;
            else if (a == Align::End)
                crossOff = crossInner - crossSize;
        }
        crossSize = std::max<int32_t>(crossSize, 0);

        const int32_t mainPos = cursor + h.margin;
        const int32_t crossPos = (row ? cy : cx) + h.margin + crossOff;

        Rect r;
        if (row)
            r = { static_cast<int16_t>(mainPos), static_cast<int16_t>(crossPos),
                  static_cast<uint16_t>(mainSize),
                  static_cast<uint16_t>(crossSize) };
        else
            r = { static_cast<int16_t>(crossPos), static_cast<int16_t>(mainPos),
                  static_cast<uint16_t>(crossSize),
                  static_cast<uint16_t>(mainSize) };

        c->setArea(r);
        c->onLayout();

        cursor += 2 * h.margin + mainSize + spacing;
    }
}

} // namespace ortxui
