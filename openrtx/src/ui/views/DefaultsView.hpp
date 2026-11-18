/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_DEFAULTSVIEW_HPP
#define ORTX_UI_DEFAULTSVIEW_HPP

#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Widgets.hpp"
#include "widgets/TopBar.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The Settings > Default Settings screen: a two-step confirmation that resets
 * all settings and the VFO channel to their factory defaults. ENTER arms the
 * confirmation, a second ENTER performs the reset (state_resetSettingsAndVfo)
 * and pops back; ESC cancels at any point. onShow() re-arms to the initial
 * prompt so a fresh entry never starts armed.
 */
class DefaultsView : public View
{
public:
    void build();
    void onShow() override;
    void announce() override;

    NavIntent onEvent(const Event &e) override;

    Screen &screen() override
    {
        return screen_;
    }

private:
    void updateText();

    Screen screen_;
    Flex root_;
    TopBar topBar_;
    Flex body_;
    Label prompt_;
    Label hint_;
    bool armed_ = false;
};

} // namespace ortxui

#endif /* ORTX_UI_DEFAULTSVIEW_HPP */
