/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_TOPBAR_HPP
#define ORTX_UI_TOPBAR_HPP

#include <cstddef>
#include <cstdint>
#include "core/Object.hpp"
#include "core/UiText.hpp"
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
 *
 * The slots are laid out as a padded row of a fixed left slot, a growing
 * centre and a fixed right slot, and their text is composed from the readings
 * when drawn, so the bar keeps only the readings and the centre's width.
 * reset() readies it for a new screen; update() follows the clock and battery
 * in the state snapshot and the keypad lock, change-gated.
 */
class TopBar : public Object
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
        return title_;
    }

    /**
     * Refresh the clock, battery and keypad lock, change-gated.
     *
     * @param s: state snapshot.
     */
    void update(const state_t &s);

    /** Size the centre slot to the bar. */
    void onLayout() override;

    /** Paint the background and the slots. */
    void draw(DrawCtx &d) override;

private:
    /**
     * Compose the centre text: the title, or the clock.
     *
     * @param clock: buffer for the clock, CLOCK_SIZE bytes.
     * @return the text.
     */
    const char *middleText(char *clock) const;

    /**
     * Get the width of the right cluster.
     *
     * @return the width, with the gap between the percentage and the icon
     * when both show.
     */
    int16_t rightWidth() const;

    /** Size of the clock text. */
    static constexpr size_t CLOCK_SIZE = 8;

    UiText title_;          ///< Page title
    uint16_t middleW_ = 0;  ///< Width of the centre slot
    uint8_t textH_ = 0;     ///< Height of the text slots
    bool hasTitle_ = false; ///< The centre shows the title, not the clock
    uint8_t hour_ = 0;      ///< Hour shown by the clock
    int8_t minute_ = -1;    ///< Minute shown by the clock, -1 before one
    uint8_t charge_ = 0xFF; ///< Battery charge shown, 0xFF before one
    uint8_t glyphs_ = 0;    ///< Left glyphs: bit 0 locked, bit 1 unread
    int8_t showIcon_ = -1;  ///< Battery as an icon; -1 shows both until set
};

} // namespace ortxui

#endif /* ORTX_UI_TOPBAR_HPP */
