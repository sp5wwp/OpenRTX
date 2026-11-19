/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/TextRow.hpp"
#include "render/DrawCtx.hpp"

namespace ortxui
{

void TextRow::setRun(uint8_t i, fontSize_t font, Sem color)
{
    runs[i].font = font;
    runs[i].color = color;
    if (i >= count)
        count = static_cast<uint8_t>(i + 1);
}

void TextRow::setText(uint8_t i, const char *text)
{
    runs[i].text = (text != nullptr) ? text : "";
}

int16_t TextRow::textWidth() const
{
    int16_t w = 0;
    uint8_t shown = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (!visible(runs[i]))
            continue;

        w = static_cast<int16_t>(
            w + gfx_getTextWidth(runs[i].font, runs[i].text));
        shown++;
    }

    if (shown > 1)
        w = static_cast<int16_t>(w + gap * (shown - 1));
    return w;
}

int16_t TextRow::baselineY() const
{
    /* Centre the line of the run with the tallest ascent and put the baseline
     * at its ascent; the fonts decide it, so it stays put as the text comes
     * and goes. */
    int16_t refAsc = -1;
    int16_t refLH = 0;
    for (uint8_t i = 0; i < count; i++) {
        const int16_t asc =
            static_cast<int16_t>(gfx_getFontAscent(runs[i].font));
        if (asc > refAsc) {
            refAsc = asc;
            refLH = static_cast<int16_t>(gfx_getFontLineHeight(runs[i].font));
        }
    }
    return static_cast<int16_t>(bounds.y + (bounds.h - refLH) / 2 + refAsc);
}

Size TextRow::natural() const
{
    int16_t h = 0;
    for (uint8_t i = 0; i < count; i++) {
        const int16_t lh =
            static_cast<int16_t>(gfx_getFontLineHeight(runs[i].font));
        if (lh > h)
            h = lh;
    }
    return { static_cast<uint16_t>(textWidth()), static_cast<uint16_t>(h) };
}

void TextRow::draw(DrawCtx &d)
{
    if (bounds.empty())
        return;

    /* right() is the last pixel, so the +1 puts the last run flush on it. */
    const int16_t baseline = baselineY();
    int16_t x = static_cast<int16_t>(bounds.right() - textWidth() + 1);
    bool first = true;
    for (uint8_t i = 0; i < count; i++) {
        const Run &r = runs[i];
        if (!visible(r))
            continue;

        if (!first)
            x = static_cast<int16_t>(x + gap);
        d.text({ x, baseline }, r.font, TEXT_ALIGN_LEFT, r.color, r.text);
        x = static_cast<int16_t>(x + gfx_getTextWidth(r.font, r.text));
        first = false;
    }
}

} // namespace ortxui
