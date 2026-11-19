/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_GPSSETTINGSVIEW_HPP
#define ORTX_UI_GPSSETTINGSVIEW_HPP

#include <array>
#include <cstdint>
#include <cstddef>
#include "core/SettingsListView.hpp"
#include "core/state.h"
#include "hwconfig.h"

namespace ortxui
{

/**
 * The Settings > GPS screen: GPS Enabled and GPS Set Time checkboxes, toggled
 * by ENTER, and the UTC timezone, stepped in half hours and shown as e.g.
 * "+1.0". The clock rows need CONFIG_RTC. The GPS position screen is GpsView.
 */
class GpsSettingsView : public SettingsListView
{
public:
    /** Constructor. */
    GpsSettingsView() = default;

    /** Destructor. */
    ~GpsSettingsView() = default;

    /** Build the rows from the live settings. */
    void build() override;

private:
    /** Get how a row is edited. */
    RowKind rowKind(uint8_t row) const override;

    /** Render the timezone. */
    void formatValue(uint8_t row, char *out, size_t cap) override;

    /** Step the timezone by half an hour. */
    void onAdjust(uint8_t row, int8_t dir) override;

    /** Apply a checkbox. */
    void onToggle(uint8_t row, bool on) override;

    /** Speak the timezone through its voice prompt helper. */
    void announceValue(uint8_t row, const UiText &value,
                       vpQueueFlags flags) override;

    /** Rows, top to bottom. */
    enum Row : uint8_t {
        RowEnabled,  ///< GPS on or off
#ifdef CONFIG_RTC
        RowSetTime,  ///< Set the clock from the GPS
        RowTimezone, ///< UTC offset in half hours
#endif
        RowCount,
    };

    static constexpr int8_t TZ_MIN = -24;       ///< Westmost timezone, -12:00
    static constexpr int8_t TZ_MAX = 28;        ///< Eastmost timezone, +14:00

    std::array<ListItem, RowCount> items_ = {}; ///< Rows
};

} // namespace ortxui

#endif /* ORTX_UI_GPSSETTINGSVIEW_HPP */
