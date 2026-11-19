/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_GPSVIEW_HPP
#define ORTX_UI_GPSVIEW_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>
#include "views/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Label.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/Compass.hpp"
#include "widgets/GpsReadout.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The GPS position screen: a compass rose showing the course over ground
 * beside a column of readouts (latitude, longitude, Maidenhead locator,
 * altitude, speed). Without a fix, because the GPS is absent, off or not yet
 * locked, a status line replaces both.
 */
class GpsView : public View
{
public:
    /** Constructor. */
    GpsView() = default;

    /** Destructor. */
    ~GpsView() = default;

    /** Build the body and the status line, showing the status line. */
    void build() override;

    /** Follow the fix, repainting only what changed. */
    void syncFromState(const state_t &s) override;

    /** Speak the full GPS fix. */
    void announce() override;

    /** The view's widget-tree root. */
    Screen &screen() override
    {
        return tree;
    }

private:
    /** What the screen shows; a change swaps the body and the status line. */
    enum class FixKind : uint8_t {
        NoGps,   ///< No GPS detected
        GpsOff,  ///< GPS turned off
        NoFix,   ///< Waiting for a fix
        FixLost, ///< The fix was lost
        Fixed,   ///< A fix
    };

    /**
     * Get what the screen shows for a state.
     *
     * @param s: state snapshot.
     * @return the fix kind.
     */
    FixKind classify(const state_t &s) const;

    /**
     * Show the body or the status line.
     *
     * @param fixed: show the body.
     */
    void showFixed(bool fixed);

    Screen tree;                       ///< Widget tree root
    Flex root;                         ///< Top bar over the body
    Flex body;                         ///< Compass beside the readouts
    Compass compass;                   ///< Course over ground
    GpsReadout readout;                ///< Position, locator, altitude, speed
    Label status;                      ///< Why there is no fix
    FixKind lastKind = FixKind::NoGps; ///< Fix kind shown
    int16_t lastHeading = INT16_MIN;   ///< Course shown
};

} // namespace ortxui

#endif /* ORTX_UI_GPSVIEW_HPP */
