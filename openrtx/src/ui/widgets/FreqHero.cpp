/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/FreqHero.hpp"
#include "base/Layout.hpp"
#include "render/DrawCtx.hpp"
#include "core/graphics.h"

#include <cstdio>
#include <cstring>

namespace ortxui
{

/** Below this width the 12 pt frequency does not fit beside the mode label. */
static constexpr uint16_t COMPACT_W = 150;

static constexpr int16_t INDENT = 4;        ///< Inset of the texts at the sides
static constexpr int16_t BASELINE_RISE = 3; ///< Baseline above the bottom
static constexpr int16_t SUB_GAP = 2;       ///< Gap by the sub-kHz digits

void FreqHero::setFreq(uint32_t hz)
{
    const uint32_t mhz = hz / 1000000UL;
    const uint32_t khz = (hz % 1000000UL) / 1000UL;
    const uint32_t sub = (hz % 1000UL) / 10UL;

    sniprintf(mainBuf, sizeof(mainBuf), "%lu.%03lu",
              static_cast<unsigned long>(mhz), static_cast<unsigned long>(khz));
    sniprintf(subBuf, sizeof(subBuf), "%02lu", static_cast<unsigned long>(sub));
    textColor = Sem::OnSurface;
}

void FreqHero::setEntry(const char *main, const char *sub)
{
    strncpy(mainBuf, main, sizeof(mainBuf) - 1);
    mainBuf[sizeof(mainBuf) - 1] = '\0';
    strncpy(subBuf, sub, sizeof(subBuf) - 1);
    subBuf[sizeof(subBuf) - 1] = '\0';
}

void FreqHero::draw(DrawCtx &d)
{
    /* The large screen has room for the largest fonts. */
    const bool large = (sizeClass() >= SizeClass::Large);
    const bool compact = (bounds.w < COMPACT_W);
    const fontSize_t mainFont = large   ? FONT_SIZE_16PT :
                                compact ? FONT_SIZE_10PT :
                                          FONT_SIZE_12PT;
    const fontSize_t subFont = large   ? FONT_SIZE_10PT :
                               compact ? FONT_SIZE_6PT :
                                         FONT_SIZE_8PT;
    const fontSize_t modeFont = subFont;

    const int16_t baseY = static_cast<int16_t>(bounds.bottom() - BASELINE_RISE);

    const uint16_t mainW = gfx_getTextWidth(mainFont, mainBuf);
    const Point mainAt = { static_cast<int16_t>(bounds.x + INDENT), baseY };
    d.text(mainAt, mainFont, TEXT_ALIGN_LEFT, textColor, mainBuf);

    int16_t modeLeft = static_cast<int16_t>(bounds.right() - INDENT);
    if (modeText[0] != '\0') {
        const uint16_t mw = gfx_getTextWidth(modeFont, modeText);
        modeLeft = static_cast<int16_t>(modeLeft - static_cast<int16_t>(mw));
        d.text({ modeLeft, baseY }, modeFont, TEXT_ALIGN_LEFT, Sem::OnSurface,
               modeText);
    }

    /* The sub-kHz digits show only if they clear the mode label. */
    const int16_t subX =
        static_cast<int16_t>(bounds.x + INDENT + mainW + SUB_GAP);
    const uint16_t subW = gfx_getTextWidth(subFont, subBuf);
    if (subX + static_cast<int16_t>(subW) <= modeLeft - SUB_GAP) {
        const Point subAt = { subX, baseY };
        d.text(subAt, subFont, TEXT_ALIGN_LEFT, textColor, subBuf);
    }
}

} // namespace ortxui
