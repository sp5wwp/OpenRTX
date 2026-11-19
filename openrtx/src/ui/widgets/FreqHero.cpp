/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/FreqHero.hpp"
#include "render/DrawCtx.hpp"
#include "core/graphics.h"

#include <cstdio>
#include <cstring>

namespace ortxui
{

/* Below this width the 12 pt frequency does not fit beside the mode label. */
static constexpr uint16_t COMPACT_W = 150;

/* Inset of the texts from the sides, and of the baseline from the bottom. */
static constexpr int16_t INDENT = 4;
static constexpr int16_t BASELINE_RISE = 3;

/* Least gap on either side of the sub-kHz digits. */
static constexpr int16_t SUB_GAP = 2;

void FreqHero::setFreq(uint32_t hz)
{
    const uint32_t mhz = hz / 1000000UL;
    const uint32_t khz = (hz % 1000000UL) / 1000UL;
    const uint32_t sub = (hz % 1000UL) / 10UL;

    sniprintf(mainBuf_, sizeof(mainBuf_), "%lu.%03lu",
              static_cast<unsigned long>(mhz), static_cast<unsigned long>(khz));
    sniprintf(subBuf_, sizeof(subBuf_), "%02lu",
              static_cast<unsigned long>(sub));
    textColor_ = Sem::OnSurface;
}

void FreqHero::setEntry(const char *main, const char *sub)
{
    strncpy(mainBuf_, main, sizeof(mainBuf_) - 1);
    mainBuf_[sizeof(mainBuf_) - 1] = '\0';
    strncpy(subBuf_, sub, sizeof(subBuf_) - 1);
    subBuf_[sizeof(subBuf_) - 1] = '\0';
}

void FreqHero::draw(DrawCtx &d)
{
    const bool compact = (area_.w < COMPACT_W);
    const fontSize_t mainFont = compact ? FONT_SIZE_10PT : FONT_SIZE_12PT;
    const fontSize_t subFont = compact ? FONT_SIZE_6PT : FONT_SIZE_8PT;
    const fontSize_t modeFont = compact ? FONT_SIZE_6PT : FONT_SIZE_8PT;

    const int16_t baseY = static_cast<int16_t>(area_.bottom() - BASELINE_RISE);

    const uint16_t mainW = gfx_getTextWidth(mainFont, mainBuf_);
    const Point mainAt = { static_cast<int16_t>(area_.x + INDENT), baseY };
    d.text(mainAt, mainFont, TEXT_ALIGN_LEFT, textColor_, mainBuf_);

    int16_t modeLeft = static_cast<int16_t>(area_.right() - INDENT);
    if (mode_[0] != '\0') {
        const uint16_t mw = gfx_getTextWidth(modeFont, mode_);
        modeLeft = static_cast<int16_t>(modeLeft - static_cast<int16_t>(mw));
        d.text({ modeLeft, baseY }, modeFont, TEXT_ALIGN_LEFT, Sem::OnSurface,
               mode_);
    }

    /* The sub-kHz digits show only if they clear the mode label. */
    const int16_t subX =
        static_cast<int16_t>(area_.x + INDENT + mainW + SUB_GAP);
    const uint16_t subW = gfx_getTextWidth(subFont, subBuf_);
    if (subX + static_cast<int16_t>(subW) <= modeLeft - SUB_GAP) {
        const Point subAt = { subX, baseY };
        d.text(subAt, subFont, TEXT_ALIGN_LEFT, textColor_, subBuf_);
    }
}

} // namespace ortxui
