/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/TextBlock.hpp"
#include "widgets/List.hpp"
#include "render/DrawCtx.hpp"

#include <cstdint>
#include <cstddef>

namespace ortxui
{

int16_t TextBlock::wrapX() const
{
    return static_cast<int16_t>(bounds.x + bounds.w - List::SCROLL_BAR_W - 1);
}

uint16_t TextBlock::textHeight() const
{
    return gfx_measureText(textFont, text, static_cast<uint16_t>(bounds.x),
                           static_cast<uint16_t>(wrapX()), SIZE_MAX);
}

int16_t TextBlock::maxOffset() const
{
    const uint16_t h = textHeight();
    return (h > bounds.h) ? static_cast<int16_t>(h - bounds.h) : 0;
}

int16_t TextBlock::offset() const
{
    const int16_t max = maxOffset();
    return (scrollPos > max) ? max : scrollPos;
}

bool TextBlock::scroll(int lines)
{
    const int16_t cur = offset();
    int next = cur + lines * gfx_getFontLineHeight(textFont);
    const int16_t max = maxOffset();
    if (next < 0)
        next = 0;
    if (next > max)
        next = max;
    if (next == cur)
        return false;

    scrollPos = static_cast<int16_t>(next);
    invalidate();
    return true;
}

void TextBlock::scrollToTop()
{
    scrollPos = 0;
    invalidate();
}

void TextBlock::scrollToEnd()
{
    scrollPos = INT16_MAX;
    invalidate();
}

void TextBlock::draw(DrawCtx &d)
{
    if (bounds.empty())
        return;

    const int16_t off = offset();
    const int baseline = bounds.y + gfx_getFontAscent(textFont) - off;
    const Point at = { bounds.x, static_cast<int16_t>(baseline) };
    d.textWrapped(at, textFont, color, text, wrapX(), bounds.y,
                  bounds.bottom());

    const uint16_t h = textHeight();
    if (h > bounds.h)
        List::drawScrollBar(d, bounds, static_cast<uint16_t>(off), bounds.h, h);
}

} // namespace ortxui
