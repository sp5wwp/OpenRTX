/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/GpsSettingsView.hpp"
#include "core/Event.hpp"
#include "interfaces/keyboard.h"
#include "hwconfig.h"

#include <cstdio>

namespace ortxui
{

void GpsSettingsView::build()
{
    static const UiText kLabels[RowCount] = {
        STR(gpsEnabled),
        STR(gpsSetTime),
        STR(UTCTimeZone),
    };

    for (uint8_t i = 0; i < RowCount; i++)
        items_[i].label = kLabels[i];

    /* GPS Enabled / GPS Set Time are booleans -> checkboxes; the timezone is a
     * value row. */
    items_[RowEnabled].checkbox = true;
    items_[RowEnabled].checked = state.settings.gps_enabled;
    items_[RowSetTime].checkbox = true;
    items_[RowSetTime].checked = state.settings.gpsSetTime;
    items_[RowTimezone].value = bufs_[RowTimezone];

    buildList(STR(gps), items_, RowCount);
    refreshValue(RowTimezone);

    lastEnabled_ = state.settings.gps_enabled;
    lastSetTime_ = state.settings.gpsSetTime;
    lastTz_ = state.settings.utc_timezone;
}

GpsSettingsView::RowKind GpsSettingsView::rowKind(uint8_t row) const
{
    return (row == RowTimezone) ? RowKind::Stepper : RowKind::Checkbox;
}

void GpsSettingsView::formatValue(uint8_t /*row*/, char *out, size_t cap)
{
    /* Only the timezone is a value row. */
    const settings_t &st = state.settings; /* live source of truth */
    int8_t tz = st.utc_timezone;
    /* int8_t so the format-truncation analysis bounds the %d width. */
    int8_t hr = (int8_t)(tz / 2);
    int8_t mn = (int8_t)((tz % 2) * 5);
    char sign = ' ';
    if (tz > 0) {
        sign = '+';
    } else if (tz < 0) {
        sign = '-';
        hr = (int8_t)(-hr);
        mn = (int8_t)(-mn);
    }
    snprintf(out, cap, "%c%d.%d", sign, hr, mn);
}

void GpsSettingsView::setValueText(uint8_t row, const char *s)
{
    snprintf(bufs_[row], sizeof(bufs_[row]), "%s", s);
}

void GpsSettingsView::onAdjust(uint8_t /*row*/, int dir)
{
    /* Only the timezone is adjustable (the booleans are checkboxes). */
    int tz = state.settings.utc_timezone + dir;
    if (tz < kTzMin)
        tz = kTzMin;
    if (tz > kTzMax)
        tz = kTzMax;
    state.settings.utc_timezone = (int8_t)tz;
    lastTz_ = state.settings.utc_timezone;
}

void GpsSettingsView::onToggle(uint8_t row, bool on)
{
    if (row == RowEnabled) {
        state.settings.gps_enabled = on;
        lastEnabled_ = on;
    } else if (row == RowSetTime) {
        state.settings.gpsSetTime = on;
        lastSetTime_ = on;
    }
}

void GpsSettingsView::syncFromState(const state_t &s)
{
    View::syncFromState(s); /* top bar */

    const settings_t &st = s.settings;
    bool changed = false;

    if (st.gps_enabled != lastEnabled_) {
        lastEnabled_ = st.gps_enabled;
        items_[RowEnabled].checked = st.gps_enabled;
        changed = true;
    }
    if (st.gpsSetTime != lastSetTime_) {
        lastSetTime_ = st.gpsSetTime;
        items_[RowSetTime].checked = st.gpsSetTime;
        changed = true;
    }
    if (!(editing_ && editRow_ == RowTimezone)
        && (st.utc_timezone != lastTz_)) {
        lastTz_ = st.utc_timezone;
        refreshValue(RowTimezone);
        changed = true;
    }

    if (changed)
        list_.invalidate();
}

} // namespace ortxui
