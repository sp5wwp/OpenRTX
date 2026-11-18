/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_GPSVIEW_HPP
#define ORTX_UI_GPSVIEW_HPP

#include <cstdint>
#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Widgets.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/TextRow.hpp"
#include "widgets/Compass.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The GPS Position screen (mockup 1): a compass rose on the left showing course
 * over ground, and a right-hand column of big value / small unit readouts
 * (latitude, longitude, Maidenhead locator, altitude, speed). When there is no
 * fix — GPS absent, disabled, or not yet locked — the readouts are replaced by
 * a single centred status line and the compass needle is hidden. A leaf view:
 * ESC pops back. All live values are change-gated in syncFromState().
 */
class GpsView : public View
{
public:
    void build();
    void syncFromState(const state_t &s) override;
    void announce() override;

    Screen &screen() override
    {
        return screen_;
    }

private:
    enum Row : uint8_t {
        RowLat,
        RowLon,
        RowLoc,
        RowAlt,
        RowSpd,
        RowCount,
    };

    /* Coarse presentation state; a change re-lays-out body vs. status line. */
    enum class FixKind : uint8_t { NoGps, GpsOff, NoFix, FixLost, Fixed };

    FixKind classify(const state_t &s) const;
    void showFixed(bool fixed);

    Screen screen_;
    Flex root_;
    TopBar topBar_;
    Flex body_;
    Compass compass_;
    Flex statsCol_;
    TextRow rows_[RowCount];
    Label status_;

    char valBufs_[RowCount][16] = {};

    FixKind lastKind_ = FixKind::NoGps;
    int32_t lastLat_ = INT32_MIN;
    int32_t lastLon_ = INT32_MIN;
    int16_t lastAlt_ = INT16_MIN;
    uint16_t lastSpeed_ = 0xFFFFu;
    int16_t lastHeading_ = INT16_MIN;
};

} // namespace ortxui

#endif /* ORTX_UI_GPSVIEW_HPP */
