/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_DISPLAYVIEW_HPP
#define ORTX_UI_DISPLAYVIEW_HPP

#include <cstdint>
#include <cstddef>
#include "core/SettingsListView.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * A settings screen in the "value row" style (mockup 2): a selectable list
 * where each row shows a setting label with its current value low-right.
 * syncFromState() keeps the values live from the state snapshot. ENTER on a row
 * enters an edit mode (the value is wrapped in <...>); UP/right raises it,
 * DOWN/left lowers it, and ENTER/ESC commits. Edits are written straight into
 * state.settings (with the display side-effect applied immediately) and
 * persisted to flash at shutdown, mirroring the classic UI. The base View
 * handles ESC/scroll when not editing.
 */
class DisplayView : public SettingsListView
{
public:
    void build();
    void syncFromState(const state_t &s) override;

private:
    enum Row : uint8_t {
        RowBrightness,
        RowContrast,
        RowSquelch,
        RowVox,
        RowTimer,   //< display standby timeout (settings.display_timer)
        RowBattery, //< battery readout: percentage or icon (showBatteryIcon)
#ifdef CONFIG_PIX_FMT_RGB565
        RowTheme, //< Instrument vs High Contrast palette (settings.highContrast)
        RowText,  //< Smooth (AA) vs Crisp (1bpp) text (settings.crispText)
#endif
        RowCount,
    };

    /** Inclusive value range and step per row. */
    struct Range {
        uint8_t min;
        uint8_t max;
        uint8_t step;
    };

    uint8_t curValue(uint8_t row) const;
    void applyValue(uint8_t row, uint8_t v); //< write settings + side-effect

    /* SettingsListView hooks. */
    void formatValue(uint8_t row, char *out, size_t cap) override;
    void setValueText(uint8_t row, const char *s) override;
    void onAdjust(uint8_t row, int dir) override;

    ListItem items_[RowCount] = {};
    char bufs_[RowCount][20] = {};
    /* build() seeds last_ with the live value of every row before any sync, so a
     * zero start never aliases a real value into a skipped first paint. */
    uint8_t last_[RowCount] = {};
};

} // namespace ortxui

#endif /* ORTX_UI_DISPLAYVIEW_HPP */
