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
#include "widgets/Label.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/Compass.hpp"
#include "views/GpsReadout.hpp"
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
    /* Coarse presentation state; a change re-lays-out body vs. status line. */
    enum class FixKind : uint8_t {
        NoGps,
        GpsOff,
        NoFix,
        FixLost,
        Fixed,
    };

    /** Constructor. */
    GpsView() = default;

    /** Destructor. */
    ~GpsView() = default;

    void build() override;
    void syncFromState(const state_t &s) override;
    void announce() override;

    Screen &screen() override
    {
        return screen_;
    }

private:
    FixKind classify(const state_t &s) const;
    void showFixed(bool fixed);

    Screen screen_;
    Flex root_;
    Flex body_;
    Compass compass_;
    GpsReadout readout_; ///< Position, locator, altitude and speed
    Label status_;

    FixKind lastKind_ = FixKind::NoGps;
    int16_t lastHeading_ = INT16_MIN;
};

} // namespace ortxui

#endif /* ORTX_UI_GPSVIEW_HPP */
