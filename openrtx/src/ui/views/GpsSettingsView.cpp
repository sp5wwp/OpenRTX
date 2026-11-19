/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/GpsSettingsView.hpp"
#include "core/voicePromptUtils.h"

#include <cstdio>

namespace ortxui
{

void GpsSettingsView::build()
{
    items_[RowEnabled].label = STR(gpsEnabled);
    items_[RowEnabled].checkbox = true;
    items_[RowEnabled].checked = state.settings.gps_enabled;
    items_[RowSetTime].label = STR(gpsSetTime);
    items_[RowSetTime].checkbox = true;
    items_[RowSetTime].checked = state.settings.gpsSetTime;
    items_[RowTimezone].label = STR(UTCTimeZone);

    buildList(STR(gps), items_.data(), RowCount);
}

GpsSettingsView::RowKind GpsSettingsView::rowKind(uint8_t row) const
{
    return (row == RowTimezone) ? RowKind::Stepper : RowKind::Checkbox;
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
    int tz = state.settings.utc_timezone + dir;
    if (tz < TZ_MIN)
        tz = TZ_MIN;
    if (tz > TZ_MAX)
        tz = TZ_MAX;
    state.settings.utc_timezone = static_cast<int8_t>(tz);
}

void GpsSettingsView::onToggle(uint8_t row, bool on)
{
    if (row == RowEnabled)
        state.settings.gps_enabled = on;
    else if (row == RowSetTime)
        state.settings.gpsSetTime = on;
}

void GpsSettingsView::announceValue(uint8_t row, const UiText &value,
                                    vpQueueFlags flags)
{
    if (row == RowTimezone)
        vp_announceTimeZone(state.settings.utc_timezone, flags);
    else
        SettingsListView::announceValue(row, value, flags);
}

} // namespace ortxui
