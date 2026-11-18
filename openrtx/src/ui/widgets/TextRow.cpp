/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/TextRow.hpp"
#include "render/DrawCtx.hpp"

#include <cstring>

namespace ortxui
{

void TextRow::setRun(uint8_t i, fontSize_t font, Sem color, Side side)
{
    if (i >= kMaxRuns)
        return;
    runs_[i].font = font;
    runs_[i].color = color;
    runs_[i].side = side;
    runs_[i].active = true;
    if (i >= count_)
        count_ = static_cast<uint8_t>(i + 1);
}

void TextRow::setText(uint8_t i, const char *text)
{
    if (i >= kMaxRuns)
        return;
    strncpy(runs_[i].text, (text != nullptr) ? text : "",
            sizeof(runs_[i].text) - 1);
    runs_[i].text[sizeof(runs_[i].text) - 1] = '\0';
}

void TextRow::setColor(uint8_t i, Sem color)
{
    if (i < kMaxRuns)
        runs_[i].color = color;
}

int16_t TextRow::baselineY() const
{
    /* Centre the tallest run's line box in the row and place the shared baseline
     * at that run's ascent, so every run bottom-aligns on the same line. The
     * reference is drawn from the configured runs (their fonts), not the current
     * text, so the baseline stays put whether or not a run currently has text —
     * layout runs before the text is set. */
    int16_t refAsc = 0;
    int16_t refLH = 0;
    int16_t bestAsc = -1;
    for (uint8_t i = 0; i < count_; i++) {
        if (!runs_[i].active)
            continue;
        const int16_t asc =
            static_cast<int16_t>(gfx_getFontAscent(runs_[i].font));
        if (asc > bestAsc) {
            bestAsc = asc;
            refAsc = asc;
            refLH = static_cast<int16_t>(gfx_getFontLineHeight(runs_[i].font));
        }
    }
    return static_cast<int16_t>(area_.y + (area_.h - refLH) / 2 + refAsc);
}

Size TextRow::natural() const
{
    /* Height is the tallest configured run's line box, independent of the
     * current text, so a layout done before text is set still reserves the row.
     * Width is the current content (visible runs) plus the intra-group gaps. */
    int16_t w = 0;
    int16_t h = 0;
    int16_t nLeft = 0;
    int16_t nRight = 0;
    for (uint8_t i = 0; i < count_; i++) {
        if (!runs_[i].active)
            continue;
        const int16_t lh =
            static_cast<int16_t>(gfx_getFontLineHeight(runs_[i].font));
        if (lh > h)
            h = lh;
        if (!visible(runs_[i]))
            continue;
        w = static_cast<int16_t>(
            w + gfx_getTextWidth(runs_[i].font, runs_[i].text));
        if (runs_[i].side == Side::Left)
            nLeft++;
        else
            nRight++;
    }
    if (nLeft > 1)
        w = static_cast<int16_t>(w + gap_ * (nLeft - 1));
    if (nRight > 1)
        w = static_cast<int16_t>(w + gap_ * (nRight - 1));
    return { static_cast<uint16_t>(w), static_cast<uint16_t>(h) };
}

void TextRow::draw(DrawCtx &d)
{
    if (area_.empty())
        return;

    const int16_t baseline = baselineY();

    /* Left group: pack left-to-right from the left inset. */
    int16_t lx = static_cast<int16_t>(area_.x + indent_);
    bool firstLeft = true;
    for (uint8_t i = 0; i < count_; i++) {
        const Run &r = runs_[i];
        if (!visible(r) || (r.side != Side::Left))
            continue;
        if (!firstLeft)
            lx = static_cast<int16_t>(lx + gap_);
        d.text({ lx, baseline }, r.font, TEXT_ALIGN_LEFT, r.color, r.text);
        lx = static_cast<int16_t>(lx + gfx_getTextWidth(r.font, r.text));
        firstLeft = false;
    }

    /* Right group: measure its total width, then pack it against the right
     * inset (drawn left-to-right within the group). right() is the inclusive
     * last pixel, so the +1 lands the final run flush on the inset edge. */
    int16_t totalR = 0;
    int16_t nRight = 0;
    for (uint8_t i = 0; i < count_; i++) {
        if (!visible(runs_[i]) || (runs_[i].side != Side::Right))
            continue;
        totalR = static_cast<int16_t>(
            totalR + gfx_getTextWidth(runs_[i].font, runs_[i].text));
        nRight++;
    }
    if (nRight > 1)
        totalR = static_cast<int16_t>(totalR + gap_ * (nRight - 1));

    int16_t rx = static_cast<int16_t>(area_.right() - indent_ - totalR + 1);
    bool firstRight = true;
    for (uint8_t i = 0; i < count_; i++) {
        const Run &r = runs_[i];
        if (!visible(r) || (r.side != Side::Right))
            continue;
        if (!firstRight)
            rx = static_cast<int16_t>(rx + gap_);
        d.text({ rx, baseline }, r.font, TEXT_ALIGN_LEFT, r.color, r.text);
        rx = static_cast<int16_t>(rx + gfx_getTextWidth(r.font, r.text));
        firstRight = false;
    }
}

} // namespace ortxui
