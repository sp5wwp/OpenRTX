/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_DISPLAYVIEW_HPP
#define ORTX_UI_DISPLAYVIEW_HPP

#include <array>
#include <cstdint>
#include <cstddef>
#include "base/SettingsListView.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The Settings > Display screen: display, squelch and battery readout
 * settings, each stepped in place. A change is written to state.settings and
 * applied to the display at once; settings reach flash at shutdown.
 */
class DisplayView : public SettingsListView
{
public:
    /** Constructor. */
    DisplayView() = default;

    /** Destructor. */
    ~DisplayView() = default;

    /** Build the rows from the live settings. */
    void build() override;

private:
    /**
     * Get a row's setting.
     *
     * @param row: row.
     * @return the value.
     */
    uint8_t curValue(uint8_t row) const;

    /**
     * Write a row's setting and apply it.
     *
     * @param row: row.
     * @param v: new value.
     */
    void applyValue(uint8_t row, uint8_t v);

    /** Render a row's value. */
    void formatValue(uint8_t row, char *out, size_t cap) override;

    /** Step a row's value within its range. */
    void onAdjust(uint8_t row, int8_t dir) override;

    /** Speak a value through its voice prompt helper, if it has one. */
    void announceValue(uint8_t row, const UiText &value,
                       vpQueueFlags flags) override;

    /** Rows, top to bottom. */
    enum Row : uint8_t {
#ifdef CONFIG_SCREEN_BRIGHTNESS
        RowBrightness, ///< Backlight level
#endif
#ifdef CONFIG_SCREEN_CONTRAST
        RowContrast, ///< Display contrast
#endif
        RowSquelch,  ///< FM squelch level
        RowTimer,    ///< Standby timeout
        RowBattery,  ///< Battery readout: percentage or icon
#ifdef CONFIG_PIX_FMT_RGB565
        RowTheme,    ///< Standard or high contrast palette
        RowText,     ///< Smooth or crisp text
#endif
        RowCount,
    };

    std::array<ListItem, RowCount> items_ = {}; ///< Rows
};

} // namespace ortxui

#endif /* ORTX_UI_DISPLAYVIEW_HPP */
