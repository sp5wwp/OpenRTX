/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/Label.hpp"
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

} // namespace ortxui
