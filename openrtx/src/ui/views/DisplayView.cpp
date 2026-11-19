/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/DisplayView.hpp"
#include "core/graphics.h"
#include "core/utils.h"
#include "interfaces/display.h"
#include "hwconfig.h"

#include <cstdio>

namespace ortxui
{

/** Row labels, in row order. */
static constexpr UiText LABELS[] = {
    STR(brightness), STR(contrast),  STR(squelch),
    STR(vox),        STR(timer),     STR(battery),
#ifdef CONFIG_PIX_FMT_RGB565
    STR(theme),      STR(textStyle),
#endif
};

/** Standby timer labels from TIMER_5S on; TIMER_OFF shows Off. */
static constexpr const char *TIMER_LABELS[] = {
    "5 s",   "10 s",  "15 s",  "20 s",   "25 s",   "30 s",   "1 min",  "2 min",
    "3 min", "4 min", "5 min", "15 min", "30 min", "45 min", "1 hour",
};

void DisplayView::build()
{
    static_assert(ARRAY_SIZE(LABELS) == RowCount, "a row has no label");

    for (uint8_t i = 0; i < RowCount; i++)
        items_[i].label = LABELS[i];
    buildList(STR(display), items_.data(), RowCount);
}

uint8_t DisplayView::curValue(uint8_t row) const
{
    switch (row) {
        case RowBrightness:
            return state.settings.brightness;
        case RowContrast:
            return state.settings.contrast;
        case RowSquelch:
            return state.settings.sqlLevel;
        case RowVox:
            return state.settings.voxLevel;
        case RowTimer:
            return state.settings.display_timer;
        case RowBattery:
            return state.settings.showBatteryIcon ? 1u : 0u;
#ifdef CONFIG_PIX_FMT_RGB565
        case RowTheme:
            return state.settings.highContrast ? 1u : 0u;
        case RowText:
            return state.settings.crispText ? 1u : 0u;
#endif
        default:
            return 0u;
    }
}

void DisplayView::applyValue(uint8_t row, uint8_t v)
{
    switch (row) {
        case RowBrightness:
            state.settings.brightness = v;
            display_setBacklightLevel(v);
            break;
        case RowContrast:
            state.settings.contrast = v;
            display_setContrast(v);
            break;
        case RowSquelch:
            state.settings.sqlLevel = v;
            break;
        case RowVox:
            state.settings.voxLevel = v;
            break;
        case RowTimer:
            /* The standby loop reads display_timer on every tick. */
            state.settings.display_timer = v;
            break;
        case RowBattery:
            state.settings.showBatteryIcon = (v != 0u);
            break;
#ifdef CONFIG_PIX_FMT_RGB565
        case RowTheme:
            /* themeColor() reads this live: repaint in the new palette. */
            state.settings.highContrast = (v != 0u);
            screen_.markAllDirty();
            break;
        case RowText:
            state.settings.crispText = (v != 0u);
            gfx_setFontMono(v != 0u);
            screen_.markAllDirty();
            break;
#endif
        default:
            break;
    }
}

void DisplayView::formatValue(uint8_t row, char *out, size_t cap)
{
    const uint8_t v = curValue(row);
    if ((row == RowVox) && (v == 0u))
        sniprintf(out, cap, "%s", STR(off).str());
    else if (row == RowSquelch)
        sniprintf(out, cap, "S%u", v);
    else if (row == RowTimer)
        sniprintf(out, cap, "%s",
                  ((v == TIMER_OFF) || (v > TIMER_1H)) ? STR(off).str() :
                                                         TIMER_LABELS[v - 1u]);
    else if (row == RowBattery)
        sniprintf(out, cap, "%s", (v ? STR(icon) : STR(percent)).str());
#ifdef CONFIG_PIX_FMT_RGB565
    else if (row == RowTheme)
        sniprintf(out, cap, "%s",
                  (v ? STR(highContrast) : STR(standard)).str());
    else if (row == RowText)
        sniprintf(out, cap, "%s", (v ? STR(crisp) : STR(smooth)).str());
#endif
    else
        sniprintf(out, cap, "%u", v);
}

void DisplayView::onAdjust(uint8_t row, int8_t dir)
{
    static constexpr Range RANGES[RowCount] = {
        { 5u, 100u, 5u },            // Brightness
        { 0u, 255u, 4u },            // Contrast
        { 0u, 15u, 1u },             // Squelch
        { 0u, 10u, 1u },             // Vox
        { TIMER_OFF, TIMER_1H, 1u }, // Timer
        { 0u, 1u, 1u },              // Battery: percent or icon
#ifdef CONFIG_PIX_FMT_RGB565
        { 0u, 1u, 1u },              // Theme: standard or high contrast
        { 0u, 1u, 1u },              // Text: smooth or crisp
#endif
    };
    const Range &r = RANGES[row];

    int v = curValue(row) + dir * r.step;
    if (v < r.min)
        v = r.min;
    if (v > r.max)
        v = r.max;

    applyValue(row, static_cast<uint8_t>(v));
}

} // namespace ortxui
