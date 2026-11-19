/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_MACROVIEW_HPP
#define ORTX_UI_MACROVIEW_HPP

#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/BarRow.hpp"
#include "widgets/TopBar.hpp"
#include "views/MacroTiles.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The macro menu: a 3x3 grid of quick radio actions, each tile showing its
 * number key, an icon and the live value. Number keys 1-9 act (tone
 * encode/decode, tone -, tone +, bandwidth, mode, power, brightness -,
 * brightness +, keypad lock); LEFT/DOWN and RIGHT/UP change the squelch in the
 * footer. ui_shim opens and closes it from MONI, so its events arrive with the
 * MONI bit set.
 */
class MacroView : public View
{
public:
    /** Constructor. */
    MacroView() = default;

    /** Destructor. */
    ~MacroView() = default;

    /** Build the widget tree. */
    void build() override;

    /**
     * Follow the tile values and the squelch.
     *
     * @param s: state snapshot.
     */
    void syncFromState(const state_t &s) override;

    /**
     * Act on a key or the knob.
     *
     * @param e: event.
     * @return no navigation: ui_shim opens and closes the menu.
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
    /**
     * Run a macro action and speak its result.
     *
     * @param number: number key of the action, 1 to 9.
     */
    void doAction(uint8_t number);

    /**
     * Step the FM squelch by one level, within its range.
     *
     * @param dir: +1 up, -1 down.
     */
    void stepSquelch(int dir);

    /**
     * Act on the number keys, and step the squelch with the arrows or the
     * knob.
     *
     * @param e: event.
     */
    void onKeypadEvent(const Event &e);

    /**
     * On radios without number keys, pick a tile with the knob and act on it
     * with ENTER, and step the squelch with the volume keys.
     *
     * @param e: event.
     */
    void onKnobEvent(const Event &e);

    Screen screen_;          ///< Widget tree
    Flex root_;              ///< Column of the bar, grid and footer
    MacroTiles tiles_;       ///< The action tiles
    Flex footer_;            ///< Row of the squelch bar
    BarRow sqlBar_;          ///< Squelch level

    char sqlBuf_[8] = { 0 }; ///< Squelch readout
    uint8_t selected_ = 0;   ///< Tile the knob picked, without number keys
};

} // namespace ortxui

#endif /* ORTX_UI_MACROVIEW_HPP */
