/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_VOLUMEOVERLAYVIEW_HPP
#define ORTX_UI_VOLUMEOVERLAYVIEW_HPP

#include <cstdint>
#include "core/View.hpp"
#include "views/VolumeHud.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The volume HUD: a title over a level bar, shown by ui_shim over the screen
 * while the volume knob turns and hidden shortly after it stops. It stays
 * built while screens come and go, keeping only the level it shows.
 */
class VolumeOverlayView : public View
{
public:
    /** Constructor. */
    VolumeOverlayView() = default;

    /** Destructor. */
    ~VolumeOverlayView() = default;

    /** Ready the HUD for its first paint. */
    void build() override;

    /**
     * Follow the volume level.
     *
     * @param s: state snapshot.
     */
    void syncFromState(const state_t &s) override;

    /**
     * Ignore an event: ui_shim hides the HUD before a key event, so only
     * status events reach it.
     *
     * @param e: event.
     * @return no navigation.
     */
    NavIntent onEvent(const Event &e) override;

    /**
     * Get the widget tree.
     *
     * @return the screen.
     */
    Screen &screen() override
    {
        return screen_;
    }

private:
    VolumeHud screen_; ///< Screen, painting the HUD
};

} // namespace ortxui

#endif /* ORTX_UI_VOLUMEOVERLAYVIEW_HPP */
