/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_VOLUMEOVERLAYVIEW_HPP
#define ORTX_UI_VOLUMEOVERLAYVIEW_HPP

#include <cstdint>
#include "core/View.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The transient volume HUD: a full-screen title + horizontal level bar shown
 * while the user turns the physical volume knob. It is a modal overlay driven
 * globally by ui_shim (the knob is a polled analogue value, not a view's own
 * input), presented with a Transient dismiss policy so it auto-hides shortly
 * after the knob stops moving. It never self-navigates. It stays built while
 * other screens come and go, so it keeps only the level it shows and paints
 * the title and bar from it.
 */
class VolumeOverlayView : public View
{
public:
    void build() override;
    void syncFromState(const state_t &s) override;
    NavIntent onEvent(const Event &e) override;

    Screen &screen() override
    {
        return screen_;
    }

private:
    /**
     * The HUD's screen: a padded column centring the title over the level
     * bar, painted from the level shown.
     */
    class HudScreen : public Screen
    {
    public:
        /** Paint the title and the level bar. */
        void draw(DrawCtx &d) override;

        int16_t volume = -1; ///< Level shown, -1 forces the first paint
    };

    HudScreen screen_; ///< Screen, painting the HUD
};

} // namespace ortxui

#endif /* ORTX_UI_VOLUMEOVERLAYVIEW_HPP */
