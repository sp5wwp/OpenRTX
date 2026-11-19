/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_MACROVIEW_HPP
#define ORTX_UI_MACROVIEW_HPP

#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Widgets.hpp"
#include "widgets/BarRow.hpp"
#include "widgets/TopBar.hpp"
#include "views/MacroTiles.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The macro menu — a graphical icon-tile grid of quick radio actions, ported
 * from the classic MONI overlay (`_ui_fsm_menuMacro`).
 *
 * A 3x3 grid of tiles: each shows its number key + an icon and the live value.
 * Number keys 1-9 act (tone enc/dec, tone -, tone +, bandwidth, mode, power,
 * brightness -, brightness +, keypad lock); LEFT/DOWN and RIGHT/UP change the
 * squelch shown in the footer. It is a modal overlay driven by a held MONI key,
 * pushed/popped by ui_shim via Navigator::openOverlay (not the normal nav
 * stack), so its events arrive with the MONI bit also set.
 */
class MacroView : public View
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
    void doAction(uint8_t number);
    void stepSquelch(int dir);

    Screen screen_;
    Flex root_;
    Flex rows_[3]; //< grid rows, sizing the tiles in the column
    MacroTiles tiles_;
    Flex footer_;
    BarRow sqlBar_; //< squelch level, using the shared level-bar control

    char sqlBuf_[8] = { 0 };
};

} // namespace ortxui

#endif /* ORTX_UI_MACROVIEW_HPP */
