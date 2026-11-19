/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/GpsSettingsView.hpp"
#include "core/voicePromptUtils.h"

#include <algorithm>
#include <cstdio>

namespace ortxui
{

void GpsSettingsView::build()
{
    items[RowEnabled].label = STR(gpsEnabled);
    items[RowEnabled].checkbox = true;
    items[RowEnabled].checked = state.settings.gps_enabled;
#ifdef CONFIG_RTC
    items[RowSetTime].label = STR(gpsSetTime);
    items[RowSetTime].checkbox = true;
    items[RowSetTime].checked = state.settings.gpsSetTime;
    items[RowTimezone].label = STR(UTCTimeZone);
#endif

    buildList(STR(gps), items.data(), RowCount);
}

GpsSettingsView::RowKind GpsSettingsView::rowKind(uint8_t row) const
{
#ifdef CONFIG_RTC
    if (row == RowTimezone)
        return RowKind::Stepper;
#endif
    (void)row;
    return RowKind::Checkbox;
}

void GpsSettingsView::formatValue(uint8_t row, char *out, size_t cap)
{
    (void)row;
    const int8_t tz = state.settings.utc_timezone;
    /* int8_t so the format-truncation analysis bounds the %d width. */
    int8_t hr = static_cast<int8_t>(tz / 2);
    int8_t mn = static_cast<int8_t>((tz % 2) * 5);
    char sign = ' ';
    if (tz > 0) {
        sign = '+';
    } else if (tz < 0) {
        sign = '-';
        hr = static_cast<int8_t>(-hr);
        mn = static_cast<int8_t>(-mn);
    }
    sniprintf(out, cap, "%c%d.%d", sign, hr, mn);
}

void GpsSettingsView::onAdjust(uint8_t row, int8_t dir)
{
    (void)row;
    const int32_t next = state.settings.utc_timezone + dir;
    const int32_t tz = std::min<int32_t>(std::max<int32_t>(next, TZ_MIN),
                                         TZ_MAX);
    state.settings.utc_timezone = static_cast<int8_t>(tz);
}

void GpsSettingsView::onToggle(uint8_t row, bool on)
{
    if (row == RowEnabled)
        state.settings.gps_enabled = on;
#ifdef CONFIG_RTC
    else if (row == RowSetTime)
        state.settings.gpsSetTime = on;
#endif
}

void GpsSettingsView::announceValue(uint8_t row, const UiText &value,
                                    vpQueueFlags flags)
{
#ifdef CONFIG_RTC
    if (row == RowTimezone) {
        vp_announceTimeZone(state.settings.utc_timezone, flags);
        return;
    }
#endif
    SettingsListView::announceValue(row, value, flags);
}

} // namespace ortxui
