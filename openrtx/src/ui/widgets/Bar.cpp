/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/Bar.hpp"
#include "render/DrawCtx.hpp"

namespace ortxui
{

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
