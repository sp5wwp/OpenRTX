/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_DEFAULTSVIEW_HPP
#define ORTX_UI_DEFAULTSVIEW_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include "base/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Label.hpp"
#include "widgets/TopBar.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The Settings > Default Settings screen: a two-step confirmation that resets
 * all settings and the VFO channel to their factory defaults. ENTER arms the
 * confirmation, a second ENTER performs the reset and goes back; ESC cancels
 * at any point.
 */
class DefaultsView : public View
{
public:
    /** Constructor. */
    DefaultsView() = default;

    /** Destructor. */
    ~DefaultsView() = default;

    /** Build the widget tree. */
    void build() override;

    /** Speak the prompt shown. */
    void announce() override;

    /** Arm, reset or cancel. */
    NavIntent onEvent(const Event &e) override;

    /** The view's widget-tree root. */
    Screen &screen() override
    {
        return screen_;
    }

private:
    /** Show the prompt and hint of the current step. */
    void updateText();

    Screen screen_;      ///< Widget-tree root
    Flex root_;          ///< Column: top bar over the body
    Flex body_;          ///< Centred prompt over the hint
    Label prompt_;       ///< What a confirmation does
    Label hint_;         ///< Keys to press
    bool armed_ = false; ///< The next ENTER resets
};

} // namespace ortxui

#endif /* ORTX_UI_DEFAULTSVIEW_HPP */
