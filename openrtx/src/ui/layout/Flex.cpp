/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "layout/Flex.hpp"
#include "render/DrawCtx.hpp"

namespace ortxui
{

void Flex::draw(DrawCtx &d)
{
    if (hasBg_)
        d.fillRect(area_, bg_);
}

int16_t Flex::childBase(const Object &c) const
{
    const LayoutHints &h = c.layout();
    if (h.basis >= 0)
        return h.basis;

    const Size nat = c.natural();
    return static_cast<int16_t>((axis_ == Axis::Row) ? nat.w : nat.h);
}

void Flex::onLayout()
{
    const bool row = (axis_ == Axis::Row);

    /* Content box, inside padding. */
    const int16_t cx = static_cast<int16_t>(area_.x + padX_);
    const int16_t cy = static_cast<int16_t>(area_.y + padY_);
    const int cw = static_cast<int>(area_.w) - 2 * padX_;
    const int ch = static_cast<int>(area_.h) - 2 * padY_;
    if ((cw <= 0) || (ch <= 0))
        return;

    const int mainAvail = row ? cw : ch;
    const int crossAvail = row ? ch : cw;

    /* First pass: sum fixed main extents (incl. margins) and grow weights. */
    int totalBase = 0;
    int totalGrow = 0;
    int count = 0;
    for (Object *c = firstChild(); c != nullptr; c = c->nextSibling()) {
        if (c->hasFlag(FLAG_HIDDEN))
            continue;
        count++;
        totalBase += childBase(*c) + 2 * c->layout().margin;
        totalGrow += c->layout().grow;
    }
    if (count == 0)
        return;

    const int gaps = gap_ * (count - 1);
    int free = mainAvail - totalBase - gaps;
    if (free < 0)
        free = 0;

    /* Main-axis start cursor and inter-item spacing. A grow weight consumes the
     * free space, so justify only redistributes when nothing grows. */
    int cursor = row ? cx : cy;
    int spacing = gap_;
    if (totalGrow == 0) {
        switch (justify_) {
            case Justify::Start:
                break;
            case Justify::Center:
                cursor += free / 2;
                break;
            case Justify::End:
                cursor += free;
                break;
            case Justify::SpaceBetween:
                if (count > 1)
                    spacing += free / (count - 1);
                break;
            case Justify::SpaceAround: {
                const int unit = free / count;
                cursor += unit / 2;
                spacing += unit;
                break;
            }
        }
    }

    /* Second pass: assign each child an absolute area and recurse. */
    for (Object *c = firstChild(); c != nullptr; c = c->nextSibling()) {
        if (c->hasFlag(FLAG_HIDDEN))
            continue;

        const LayoutHints &h = c->layout();
        int mainSize = childBase(*c);
        if ((totalGrow > 0) && (h.grow > 0))
            mainSize +=
                static_cast<int>(static_cast<long>(free) * h.grow / totalGrow);

        /* Cross-axis size and offset. */
        const Align a = (h.self == Align::Auto) ? align_ : h.self;
        const int crossInner = crossAvail - 2 * h.margin;
        const Size nat = c->natural();
        int crossSize = row ? nat.h : nat.w;
        int crossOff = 0;
        if (a == Align::Stretch) {
            crossSize = crossInner;
        } else {
            if (crossSize > crossInner)
                crossSize = crossInner;
            if (a == Align::Center)
                crossOff = (crossInner - crossSize) / 2;
            else if (a == Align::End)
                crossOff = crossInner - crossSize;
        }
        if (crossSize < 0)
            crossSize = 0;

        const int mainPos = cursor + h.margin;
        const int crossPos = (row ? cy : cx) + h.margin + crossOff;

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
        c->onLayout(); /* recurse into nested containers */

        cursor += 2 * h.margin + mainSize + spacing;
    }
}

} // namespace ortxui
