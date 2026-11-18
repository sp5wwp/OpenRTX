/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/Widgets.hpp"
#include "render/DrawCtx.hpp"

namespace ortxui
{

Size Label::natural() const
{
    /* Reserve the full line height (ascent + descent), not just the ascent, so
     * a layout that centres the label leaves room for descenders below the
     * baseline instead of clipping them at the frame edge. */
    return { gfx_getTextWidth(font_, text_.str()),
             gfx_getFontLineHeight(font_) };
}

void Label::draw(DrawCtx &d)
{
    if (overflow_ == Overflow::Ellipsize)
        d.textInBoxEllipsized(area_, font_, align_, color_, text_.str());
    else
        d.textInBox(area_, font_, align_, color_, text_.str());
}

void Panel::draw(DrawCtx &d)
{
    d.fillRect(area_, color_);
}

void Bar::draw(DrawCtx &d)
{
    d.fillRect(area_, track_);

    float v = value_;
    if (v < 0.0f)
        v = 0.0f;
    if (v > 1.0f)
        v = 1.0f;

    const uint16_t fillW = static_cast<uint16_t>(area_.w * v);
    if (fillW > 0) {
        const Rect fill = { area_.x, area_.y, fillW, area_.h };
        d.fillRect(fill, fill_);
    }
}

} // namespace ortxui
