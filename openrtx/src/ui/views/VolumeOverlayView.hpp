/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_VOLUMEOVERLAYVIEW_HPP
#define ORTX_UI_VOLUMEOVERLAYVIEW_HPP

#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Widgets.hpp"
#include "widgets/BarRow.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The transient volume HUD: a full-screen title + horizontal level bar shown
 * while the user turns the physical volume knob. It is a modal overlay driven
 * globally by ui_shim (the knob is a polled analogue value, not a view's own
 * input), presented with a Transient dismiss policy so it auto-hides shortly
 * after the knob stops moving. It never self-navigates.
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
    Screen screen_;
    Flex root_;
    Label title_;
    BarRow bar_;

    char titleBuf_[32] = { 0 }; //< volume glyph + the volume label
    char readoutBuf_[8] = { 0 };
    int16_t shownVol_ = -1; //< last-rendered volume (−1 forces the first paint)
};

} // namespace ortxui

#endif /* ORTX_UI_VOLUMEOVERLAYVIEW_HPP */
