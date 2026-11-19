/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/VolumeHud.hpp"
#include "core/Layout.hpp"
#include "core/UiText.hpp"
#include "core/graphics.h"
#include "render/DrawCtx.hpp"
#include "style/Symbols.hpp"
#include "widgets/BarRow.hpp"
#include "widgets/Scratch.hpp"
#include "hwconfig.h"

#include <cstdio>

namespace ortxui
{

/* Whether the HUD is laid out for the roomy screen. */
static constexpr bool REGULAR = (sizeClass() == SizeClass::Regular);

/* Inset of the column from the screen edges. */
static constexpr int16_t PAD_X = 6;
static constexpr int16_t PAD_Y = 4;

/* Gap between the title and the bar. */
static constexpr int16_t GAP = REGULAR ? 6 : 3;

/* Heights of the title and of the bar. */
static constexpr int16_t TITLE_H = REGULAR ? 20 : 12;
static constexpr int16_t BAR_H = REGULAR ? 16 : 10;

void VolumeHud::draw(DrawCtx &d)
{
    /* The title and the bar, centred down the padded screen and stretched
     * across it. */
    const int16_t w = CONFIG_SCREEN_WIDTH - 2 * PAD_X;
    int free = CONFIG_SCREEN_HEIGHT - 2 * PAD_Y - TITLE_H - BAR_H - GAP;
    if (free < 0)
        free = 0;
    const int16_t titleY = static_cast<int16_t>(PAD_Y + free / 2);
    const Rect title = { PAD_X, titleY, static_cast<uint16_t>(w),
                         static_cast<uint16_t>(TITLE_H) };
    const Rect bar = { PAD_X, static_cast<int16_t>(titleY + TITLE_H + GAP),
                       static_cast<uint16_t>(w), static_cast<uint16_t>(BAR_H) };

    /* The title mixes a glyph with a word, composed in the current
     * language. */
    char text[32];
    sniprintf(text, sizeof(text), SYMBOL_VOLUME " %s", STR(volume).str());
    d.pushClip(title);
    d.textInBox(title, REGULAR ? FONT_SIZE_12PT : FONT_SIZE_8PT,
                TEXT_ALIGN_CENTER, Sem::Accent, text);
    d.popClip();

    const unsigned level = (volume_ < 0) ? 0u : static_cast<unsigned>(volume_);
    char readout[8] = "";
    if (volume_ >= 0)
        sniprintf(readout, sizeof(readout), "%u%%",
                  (level * 100u + 127u) / 255u);

    BarRow &row = Scratch::get<BarRow>();
    row.setLabel(UiText());
    row.setFont(REGULAR ? FONT_SIZE_8PT : FONT_SIZE_6PT);
    row.setColors(Sem::SurfaceHigh, Sem::Primary);
    row.setTextColor(Sem::OnSurface);
    row.setReadout(readout);
    row.setValue(level / 255.0f);
    row.setArea(bar);
    row.paintTree(d);

    /* The readout lived on this stack frame. */
    row.setReadout("");
}

} // namespace ortxui
