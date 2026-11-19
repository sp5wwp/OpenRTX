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

#include "views/View.hpp"
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
        return tree;
    }

private:
    /** Show the prompt and hint of the current step. */
    void updateText();

    Screen tree;        ///< Widget-tree root
    Flex root;          ///< Column: top bar over the body
    Flex body;          ///< Centred prompt over the hint
    Label prompt;       ///< What a confirmation does
    Label hint;         ///< Keys to press
    bool armed = false; ///< The next ENTER resets
};

} // namespace ortxui

#endif /* ORTX_UI_DEFAULTSVIEW_HPP */
