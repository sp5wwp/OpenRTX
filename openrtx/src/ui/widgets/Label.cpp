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
    /* The full line height, so a centred label keeps its descenders. */
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
