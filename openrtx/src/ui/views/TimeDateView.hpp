/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_TIMEDATEVIEW_HPP
#define ORTX_UI_TIMEDATEVIEW_HPP

#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Widgets.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/TextInput.hpp"
#include "core/state.h"
#include "core/datetime.h"

namespace ortxui
{

/**
 * The Settings > Time & Date screen (guarded by CONFIG_RTC). Displays the
 * current local date/time; ENTER opens a keypad digit editor (dd/mm/yy hh:mm,
 * seconds fixed at 00, ported from the classic SETTINGS_TIMEDATE_SET flow).
 * Digits fill the fields left to right; a full entry committed with ENTER is
 * converted back to UTC and applied via platform_setTime. ESC cancels the edit
 * (or pops the view when not editing).
 */
class TimeDateView : public View
{
public:
    void build();
    void onShow() override;
    void announce() override;
    void syncFromState(const state_t &s) override;

    NavIntent onEvent(const Event &e) override;

    Screen &screen() override
    {
        return screen_;
    }

private:
    void showDisplay(const state_t &s, bool force);
    void beginEdit();
    void commit();
    void refreshEdit();

    Screen screen_;
    Flex root_;
    TopBar topBar_;
    Flex body_;
    Label dateLabel_;
    Label timeLabel_;
    Label hint_;

    bool editing_ = false;
    /* Digit entry on the shared TextInput slot engine: 10 slots (dd mm yy hh mm),
     * seconds fixed at 00. Gains '*' backspace over the old positional filler. */
    TextInput input_;
    char inputBuf_[12] = { 0 };
    char dateBuf_[16] = { 0 };   //< "dd/mm/yy" with '_' placeholders
    char timeBuf_[16] = { 0 };   //< "hh:mm:00" with '_' placeholders
    char dispCache_[32] = { 1 }; //< change-gate for the display-mode strings
};

} // namespace ortxui

#endif /* ORTX_UI_TIMEDATEVIEW_HPP */
