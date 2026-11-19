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
    return static_cast<int16_t>(area_.x + area_.w - List::SCROLL_BAR_W - 1);
}

uint16_t TextBlock::textHeight() const
{
    return gfx_measureText(font_, text_, static_cast<uint16_t>(area_.x),
                           static_cast<uint16_t>(wrapX()), SIZE_MAX);
}

int16_t TextBlock::maxOffset() const
{
    const uint16_t h = textHeight();
    return (h > area_.h) ? static_cast<int16_t>(h - area_.h) : 0;
}

int16_t TextBlock::offset() const
{
    const int16_t max = maxOffset();
    return (scroll_ > max) ? max : scroll_;
}

bool TextBlock::scroll(int lines)
{
    const int16_t cur = offset();
    int next = cur + lines * gfx_getFontLineHeight(font_);
    const int16_t max = maxOffset();
    if (next < 0)
        next = 0;
    if (next > max)
        next = max;
    if (next == cur)
        return false;

    scroll_ = static_cast<int16_t>(next);
    invalidate();
    return true;
}

void TextBlock::scrollToTop()
{
    scroll_ = 0;
    invalidate();
}

void TextBlock::scrollToEnd()
{
    scroll_ = INT16_MAX;
    invalidate();
}

void TextBlock::draw(DrawCtx &d)
{
    if (area_.empty())
        return;

    const int16_t off = offset();
    const int baseline = area_.y + gfx_getFontAscent(font_) - off;
    const Point at = { area_.x, static_cast<int16_t>(baseline) };
    d.textWrapped(at, font_, color_, text_, wrapX(), area_.y, area_.bottom());

    const uint16_t h = textHeight();
    if (h > area_.h)
        List::drawScrollBar(d, area_, static_cast<uint16_t>(off), area_.h, h);
}

} // namespace ortxui
