/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/BarRow.hpp"
#include "render/DrawCtx.hpp"

#include <cstdio>

namespace ortxui
{

namespace
{
constexpr int16_t kPad = 3;   //< outer gutter
constexpr int16_t kGap = 5;   //< gap around the label / readout
constexpr uint16_t kBarH = 8; //< track thickness
} // namespace

Size BarRow::natural() const
{
    return { 200, static_cast<uint16_t>(gfx_getFontLineHeight(font_)) };
}

void BarRow::draw(DrawCtx &d)
{
    int16_t left = static_cast<int16_t>(area_.x + kPad);
    int16_t right = static_cast<int16_t>(area_.x + area_.w - kPad);

    /* Label pinned to the left, after its glyph if it has one. */
    char joined[32];
    const char *label = label_.str();
    if (icon_ != nullptr) {
        snprintf(joined, sizeof(joined), "%s %s", icon_, label);
        label = joined;
    }
    if (label[0] != '\0') {
        const uint16_t lw = gfx_getTextWidth(font_, label);
        d.textInBox({ left, area_.y, lw, area_.h }, font_, TEXT_ALIGN_LEFT,
                    text_, label);
        left = static_cast<int16_t>(left + lw + kGap);
    }

    /* Readout pinned to the right. */
    if (readout_[0] != '\0') {
        const uint16_t rw = gfx_getTextWidth(font_, readout_);
        d.textInBox({ static_cast<int16_t>(right - rw), area_.y, rw, area_.h },
                    font_, TEXT_ALIGN_RIGHT, text_, readout_);
        right = static_cast<int16_t>(right - rw - kGap);
    }

    if (right <= left)
        return;

    /* Track spans the space between the label and the readout, vertically
     * centred; the fill covers `value` of its width. */
    const uint16_t trackW = static_cast<uint16_t>(right - left);
    const uint16_t barH = (area_.h < kBarH) ? area_.h : kBarH;
    const int16_t barY = static_cast<int16_t>(area_.y + (area_.h - barH) / 2);
    const uint16_t radius = static_cast<uint16_t>(barH / 2);

    d.fillRoundRect({ left, barY, trackW, barH }, radius, track_);

    float v = value_;
    if (v < 0.0f)
        v = 0.0f;
    if (v > 1.0f)
        v = 1.0f;

    const uint16_t fillW = static_cast<uint16_t>(trackW * v);
    if (fillW > 0)
        d.fillRoundRect({ left, barY, fillW, barH }, radius, fill_);
}

} // namespace ortxui
