/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_INFOVIEW_HPP
#define ORTX_UI_INFOVIEW_HPP

#include <cstdint>
#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Widgets.hpp"
#include "widgets/TopBar.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The Info screen: a title bar over a table of key/value rows (firmware
 * battery, RSSI, heap, band, hardware version). Each row is a Flex row
 * [key | value] and the rows share the space equally (grow), so the table fills
 * the screen without hand-placed coordinates or scrolling. A leaf view: ESC
 * pops back. syncFromState() refreshes the live values (battery/charge/RSSI/
 * heap) change-gated; the rest are filled once at build. (The firmware version
 * is shown on the About screen rather than duplicated here.)
 */
class InfoView : public View
{
public:
    void build();
    void syncFromState(const state_t &s) override;

    Screen &screen() override
    {
        return screen_;
    }

private:
    /* Row order; the live rows are refreshed in syncFromState(). */
    enum Row : uint8_t {
        RowBattery,
        RowCharge,
        RowRssi,
        RowHeap,
        RowBand,
        RowHwVer,
        RowCount,
    };

    Screen screen_;
    Flex root_;
    TopBar topBar_;
    Flex rows_[RowCount];
    Label keys_[RowCount];
    Label vals_[RowCount];
    char valBufs_[RowCount][20] = {};

    uint16_t lastVbat_ = 0xFFFFu;
    uint8_t lastCharge_ = 0xFFu;
    int32_t lastRssi_ = INT32_MIN;
    unsigned lastHeapUsed_ = 0xFFFFFFFFu;
};

} // namespace ortxui

#endif /* ORTX_UI_INFOVIEW_HPP */
