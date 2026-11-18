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

void FreqHero::setFreq(uint32_t hz)
{
    const uint32_t mhz = hz / 1000000UL;
    const uint32_t khz = (hz % 1000000UL) / 1000UL; //< 3-digit kHz
    const uint32_t sub = (hz % 1000UL) / 10UL;      //< 2-digit, 10 Hz res

    snprintf(mainBuf_, sizeof(mainBuf_), "%lu.%03lu", (unsigned long)mhz,
             (unsigned long)khz);
    snprintf(subBuf_, sizeof(subBuf_), "%02lu", (unsigned long)sub);
    textColor_ = Sem::OnSurface; //< the live readout is never left tinted
}

void FreqHero::setEntry(const char *main, const char *sub)
{
    strncpy(mainBuf_, (main != nullptr) ? main : "", sizeof(mainBuf_) - 1);
    mainBuf_[sizeof(mainBuf_) - 1] = '\0';
    strncpy(subBuf_, (sub != nullptr) ? sub : "", sizeof(subBuf_) - 1);
    subBuf_[sizeof(subBuf_) - 1] = '\0';
}

void FreqHero::draw(DrawCtx &d)
{
    /* Narrow screens (128px Compact: gd77/dm1801) cannot fit the 12pt frequency
     * next to the mode label, so shrink the frequency on a width threshold. */
    const bool compact = (area_.w < 150);
    const fontSize_t mainFont = compact ? FONT_SIZE_10PT : FONT_SIZE_12PT;
    const fontSize_t subFont = compact ? FONT_SIZE_6PT : FONT_SIZE_8PT;
    const fontSize_t modeFont = compact ? FONT_SIZE_6PT : FONT_SIZE_8PT;

    /* Frequency, sub-digits and the mode label all share one baseline near the
     * bottom of the band. */
    const int16_t indent = 4;
    const int16_t baseY = static_cast<int16_t>(area_.bottom() - 3);

    const uint16_t mainW = gfx_getTextWidth(mainFont, mainBuf_);
    const Point mainAt = { static_cast<int16_t>(area_.x + indent), baseY };
    d.text(mainAt, mainFont, TEXT_ALIGN_LEFT, textColor_, mainBuf_);

    /* Mode/bandwidth label (WFM/NFM/M17), right-aligned on the baseline. */
    int16_t modeLeft = static_cast<int16_t>(area_.right() - indent);
    if (mode_[0] != '\0') {
        const uint16_t mw = gfx_getTextWidth(modeFont, mode_);
        modeLeft = static_cast<int16_t>(area_.right() - indent - (int16_t)mw);
        d.text({ modeLeft, baseY }, modeFont, TEXT_ALIGN_LEFT, Sem::OnSurface,
               mode_);
    }

    /* Only draw the trailing sub-kHz digits if they clear the mode label. */
    const int16_t subX = static_cast<int16_t>(area_.x + indent + mainW + 2);
    const uint16_t subW = gfx_getTextWidth(subFont, subBuf_);
    if (subX + (int16_t)subW <= modeLeft - 2) {
        const Point subAt = { subX, baseY };
        d.text(subAt, subFont, TEXT_ALIGN_LEFT, textColor_, subBuf_);
    }
}

} // namespace ortxui
