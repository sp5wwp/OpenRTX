/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/DisplayView.hpp"
#include "core/graphics.h"
#include "core/utils.h"
#include "core/voicePromptUtils.h"
#include "interfaces/display.h"
#include "hwconfig.h"

#include <cstdio>

namespace ortxui
{

/** Label and inclusive value range of a row. */
struct RowInfo {
    UiText label; ///< Row label
    uint8_t min;  ///< Lowest value
    uint8_t max;  ///< Highest value
    uint8_t step; ///< Step of one key press
};

/** Rows, in row order. */
static constexpr RowInfo ROWS[] = {
#ifdef CONFIG_SCREEN_BRIGHTNESS
    { STR(brightness), 5u, 100u, 5u },
#endif
#ifdef CONFIG_SCREEN_CONTRAST
    { STR(contrast), 0u, 255u, 4u },
#endif
    { STR(squelch), 0u, 15u, 1u },     { STR(timer), TIMER_OFF, TIMER_1H, 1u },
    { STR(battery), 0u, 1u, 1u },   // percent or icon
#ifdef CONFIG_PIX_FMT_RGB565
    { STR(theme), 0u, 1u, 1u },     // standard or high contrast
    { STR(textStyle), 0u, 1u, 1u }, // smooth or crisp
#endif
};

/** Standby timer labels, indexed by display_timer_t. */
static constexpr UiText TIMER_LABELS[] = {
    STR(off),        STR(timer5s),    STR(timer10s),   STR(timer15s),
    STR(timer20s),   STR(timer25s),   STR(timer30s),   STR(timer1min),
    STR(timer2min),  STR(timer3min),  STR(timer4min),  STR(timer5min),
    STR(timer15min), STR(timer30min), STR(timer45min), STR(timer1hour),
};

void DisplayView::build()
{
    static_assert(ARRAY_SIZE(ROWS) == RowCount, "a row has no label");
    static_assert(ARRAY_SIZE(TIMER_LABELS) == TIMER_1H + 1,
                  "a timer has no label");

    for (uint8_t i = 0; i < RowCount; i++)
        items[i].label = ROWS[i].label;
    buildList(STR(display), items.data(), RowCount);
}

uint8_t DisplayView::curValue(uint8_t row) const
{
    switch (row) {
#ifdef CONFIG_SCREEN_BRIGHTNESS
        case RowBrightness:
            return state.settings.brightness;
#endif
#ifdef CONFIG_SCREEN_CONTRAST
        case RowContrast:
            return state.settings.contrast;
#endif
        case RowSquelch:
            return state.settings.sqlLevel;
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
#ifdef CONFIG_SCREEN_BRIGHTNESS
        case RowBrightness:
            state.settings.brightness = v;
            display_setBacklightLevel(v);
            break;
#endif
#ifdef CONFIG_SCREEN_CONTRAST
        case RowContrast:
            state.settings.contrast = v;
            display_setContrast(v);
            break;
#endif
        case RowSquelch:
            state.settings.sqlLevel = v;
            requestSyncRtx();
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
            tree.markAllDirty();
            break;
        case RowText:
            state.settings.crispText = (v != 0u);
            gfx_setFontMono(v != 0u);
            /* The other font has other metrics: lay the screen out again. */
            root.onLayout();
            tree.markAllDirty();
            break;
#endif
        default:
            break;
    }
}

void DisplayView::formatValue(uint8_t row, char *out, size_t cap)
{
    const uint8_t v = curValue(row);
    if (row == RowTimer)
        sniprintf(out, cap, "%s", TIMER_LABELS[v].str());
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
    const RowInfo &r = ROWS[row];

    int v = curValue(row) + dir * r.step;
    if (v < r.min)
        v = r.min;
    if (v > r.max)
        v = r.max;

    applyValue(row, static_cast<uint8_t>(v));
}

void DisplayView::announceValue(uint8_t row, const UiText &value,
                                vpQueueFlags flags)
{
    switch (row) {
#ifdef CONFIG_SCREEN_BRIGHTNESS
        case RowBrightness:
            vp_announceSettingsInt(&currentLanguage->brightness, flags,
                                   state.settings.brightness);
            break;
#endif
#ifdef CONFIG_SCREEN_CONTRAST
        case RowContrast:
            vp_announceSettingsInt(&currentLanguage->contrast, flags,
                                   state.settings.contrast);
            break;
#endif
        case RowSquelch:
            vp_announceSquelch(state.settings.sqlLevel, flags);
            break;
        case RowTimer:
            vp_announceDisplayTimer();
            break;
        default:
            SettingsListView::announceValue(row, value, flags);
            break;
    }
}

} // namespace ortxui
