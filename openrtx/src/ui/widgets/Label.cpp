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
    return { gfx_getTextWidth(textFont, labelText.str()),
             gfx_getFontLineHeight(textFont) };
}

void Label::draw(DrawCtx &d)
{
    if (overflow == Overflow::Ellipsize)
        d.textInBoxEllipsized(bounds, textFont, align, color, labelText.str());
    else
        d.textInBox(bounds, textFont, align, color, labelText.str());
}

} // namespace ortxui
