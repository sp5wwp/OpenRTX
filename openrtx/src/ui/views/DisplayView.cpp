/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/DisplayView.hpp"
#include "core/Event.hpp"
#include "core/graphics.h"
#include "interfaces/keyboard.h"
#include "interfaces/display.h"
#include "hwconfig.h"

#include <cstdio>

namespace ortxui
{

void DisplayView::build()
{
    static const UiText kLabels[RowCount] = {
        STR(brightness), STR(contrast),  STR(squelch),
        STR(vox),        STR(timer),     STR(battery),
#ifdef CONFIG_PIX_FMT_RGB565
        STR(theme),      STR(textStyle),
#endif
    };

    for (uint8_t i = 0; i < RowCount; i++) {
        items_[i].label = kLabels[i];
        items_[i].value = bufs_[i];
    }
    buildList(STR(display), items_, RowCount);

    /* Seed the value strings and the change-gate from the live settings so no
     * row starts blank (last_'s 0xFF sentinel would otherwise alias a real value
     * like contrast = 255). */
    for (uint8_t i = 0; i < RowCount; i++) {
        refreshValue(i);
        last_[i] = curValue(i);
    }
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
    /* Write straight into the live settings and apply the display change now,
     * as the classic UI does; flash persistence happens at shutdown. */
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
            /* No immediate side-effect: the standby loop reads display_timer
             * live each tick. */
            state.settings.display_timer = v;
            break;
        case RowBattery:
            state.settings.showBatteryIcon = (v != 0u);
            break;
#ifdef CONFIG_PIX_FMT_RGB565
        case RowTheme:
            /* themeColor() reads this live; repaint the whole screen so the new
             * palette takes effect immediately. */
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
    /* Standby-timer labels from 5 s on, mirroring the classic
     * display_timer_values table; 0 is Off. */
    static const char *const kTimerLabels[] = {
        "5 s",   "10 s",   "15 s",   "20 s",   "25 s",
        "30 s",  "1 min",  "2 min",  "3 min",  "4 min",
        "5 min", "15 min", "30 min", "45 min", "1 hour",
    };

    const uint8_t v = curValue(row);
    if ((row == RowVox) && (v == 0u))
        snprintf(out, cap, "%s", STR(off).str());
    else if (row == RowSquelch)
        snprintf(out, cap, "S%u", v);
    else if (row == RowTimer)
        snprintf(out, cap, "%s",
                 ((v == 0u) || (v >= 16u)) ? STR(off).str() :
                                             kTimerLabels[v - 1u]);
    else if (row == RowBattery)
        snprintf(out, cap, "%s", (v ? STR(icon) : STR(percent)).str());
#ifdef CONFIG_PIX_FMT_RGB565
    else if (row == RowTheme)
        snprintf(out, cap, "%s", (v ? STR(highContrast) : STR(standard)).str());
    else if (row == RowText)
        snprintf(out, cap, "%s", (v ? STR(crisp) : STR(smooth)).str());
#endif
    else
        snprintf(out, cap, "%u", v);
}

void DisplayView::setValueText(uint8_t row, const char *s)
{
    snprintf(bufs_[row], sizeof(bufs_[row]), "%s", s);
}

void DisplayView::onAdjust(uint8_t row, int dir)
{
    static const Range kRanges[RowCount] = {
        { 5u, 100u, 5u }, //< Brightness
        { 0u, 255u, 4u }, //< Contrast
        { 0u, 15u, 1u },  //< Squelch
        { 0u, 10u, 1u },  //< Vox
        { 0u, 15u, 1u },  //< Timer (TIMER_OFF..TIMER_1H)
        { 0u, 1u, 1u },   //< Battery (Percent / Icon)
#ifdef CONFIG_PIX_FMT_RGB565
        { 0u, 1u, 1u },   //< Theme (Standard / High Contrast)
        { 0u, 1u, 1u },   //< Text (Smooth / Crisp)
#endif
    };
    const Range &r = kRanges[row];

    int v = (int)curValue(row) + dir * (int)r.step;
    if (v < (int)r.min)
        v = r.min;
    if (v > (int)r.max)
        v = r.max;

    applyValue(row, (uint8_t)v);
    last_[row] = (uint8_t)v; /* keep syncFromState from reformatting */
}

void DisplayView::syncFromState(const state_t &s)
{
    View::syncFromState(s); /* top bar */

    const settings_t &st = s.settings;
    const uint8_t vals[RowCount] = {
        st.brightness,
        st.contrast,
        st.sqlLevel,
        st.voxLevel,
        (uint8_t)st.display_timer,
        (uint8_t)(st.showBatteryIcon ? 1u : 0u),
#ifdef CONFIG_PIX_FMT_RGB565
        (uint8_t)(st.highContrast ? 1u : 0u),
        (uint8_t)(st.crispText ? 1u : 0u),
#endif
    };
    bool changed = false;

    for (uint8_t i = 0; i < RowCount; i++) {
        /* Don't clobber the row the user is actively editing. */
        if (editing_ && (i == editRow_))
            continue;
        if (vals[i] == last_[i])
            continue;

        refreshValue(i);
        last_[i] = vals[i];
        changed = true;
    }

    if (changed)
        list_.invalidate();
}

} // namespace ortxui
