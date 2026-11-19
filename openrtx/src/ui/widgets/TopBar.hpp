/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_TOPBAR_HPP
#define ORTX_UI_TOPBAR_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>
#include "base/Object.hpp"
#include "base/UiText.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The shared screen header: the keypad lock and unread-message glyphs at the
 * left, the page title (or the clock) in the centre, and the battery at the
 * right. The text is composed when drawn, so the bar keeps only the readings.
 */
class TopBar : public Object
{
public:
    /** Constructor. */
    TopBar();

    /** Destructor. */
    ~TopBar() = default;

    /**
     * Ready the bar for a new screen, as if freshly built: set the title and
     * forget what update() last showed, so it refreshes everything.
     *
     * @param title: page title; empty shows the clock instead.
     */
    void reset(const UiText &title);

    /**
     * Get the page title.
     *
     * @return the title.
     */
    const UiText &title() const
    {
        return titleText;
    }

    /**
     * Refresh the clock, battery and keypad lock, change-gated.
     *
     * @param s: state snapshot.
     * @param locked: whether the keypad is locked.
     */
    void update(const state_t &s, bool locked);

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
     * Get the width of the right slot.
     *
     * @return the width of the icon or of the widest percentage.
     */
    int16_t rightWidth() const;

    /** Size of the clock text. */
    static constexpr size_t CLOCK_SIZE = 8;

    /** Minute before the clock is first shown. */
    static constexpr int8_t NO_MINUTE = -1;

    /** Charge before the battery is first shown. */
    static constexpr uint8_t NO_CHARGE = UINT8_MAX;

    UiText titleText;           ///< Page title
    uint16_t middleW = 0;       ///< Width of the centre slot
    uint8_t textH = 0;          ///< Height of the text slots
    bool hasTitle = false;      ///< The centre shows the title, not the clock
    uint8_t hour = 0;           ///< Hour shown by the clock
    int8_t minute = NO_MINUTE;  ///< Minute shown by the clock
    uint8_t charge = NO_CHARGE; ///< Battery charge shown
    uint8_t shownGlyphs = 0;    ///< Left glyphs: bit 0 locked, bit 1 unread
    bool iconShown = false;     ///< The battery shows as an icon
};

} // namespace ortxui

#endif /* ORTX_UI_TOPBAR_HPP */
