/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_TOPBAR_HPP
#define ORTX_UI_TOPBAR_HPP

#include <cstdint>
#include "layout/Flex.hpp"
#include "widgets/Widgets.hpp"
#include "widgets/BatteryIcon.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The shared screen header: a Surface-backed row with a centred element and a
 * right-aligned status cluster. The centre shows the page title, or — when the
 * screen has no title (the VFO) — the clock. The far-left slot shows the
 * keypad-lock glyph while locked and an envelope while the inbox holds unread
 * messages, on every screen. The right cluster shows the battery as EITHER a
 * percentage or an icon, per settings.showBatteryIcon. A left pad balances the
 * cluster so the centre lands near the true middle while long titles still fit.
 * The constructor sizes the bar for the active SizeClass; reset() readies it
 * for a new screen; update() refreshes the live clock/battery/lock from the
 * state snapshot, change-gated.
 */
class TopBar : public Flex
{
public:
    /** Constructor. */
    TopBar();

    /** Destructor. */
    ~TopBar();

    /**
     * Ready the bar for a new screen, as if freshly built: set the title and
     * forget what update() last showed, so it refreshes everything.
     *
     * @param title: page title; empty shows the clock instead.
     */
    void reset(const UiText &title);

    /**
     * Change the page title.
     *
     * @param t: page title; empty shows the clock instead.
     */
    void setTitle(const UiText &t);

    /**
     * Get the page title.
     *
     * @return the title.
     */
    const UiText &title() const
    {
        return titleStr_;
    }

    /** Refresh clock + battery + lock from the state snapshot (change-gated). */
    void update(const state_t &s);

private:
    Label lock_;   //< lock / unread glyphs at the far left (blank otherwise);
                   //  its reserved width also balances the right status cluster
    Label middle_; //< page title, or the clock when there is no title
    Label pct_;    //< battery percentage (when showBatteryIcon is false)
    BatteryIcon battery_; //< battery glyph (when showBatteryIcon is true)

    UiText titleStr_;
    bool hasTitle_ = false;
    char clockBuf_[8] = { 0 };
    char pctBuf_[8] = { 0 };
    int16_t lastMinute_ = -1;
    uint8_t lastCharge_ = 0xFFu;
    bool lastLocked_ = false;
    /** Whether the envelope for unread messages is shown. */
    bool lastUnread_ = false;
    int8_t lastShowIcon_ = -1; //< -1 forces the first battery-mode layout
};

} // namespace ortxui

#endif /* ORTX_UI_TOPBAR_HPP */
