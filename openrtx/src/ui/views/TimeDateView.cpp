/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/TimeDateView.hpp"
#include "core/Event.hpp"
#include "interfaces/keyboard.h"
#include "interfaces/platform.h"
#include "hwconfig.h"

#include <cstdio>
#include <cstring>

namespace ortxui
{

/* KEY_0..KEY_9 occupy bits 0..9, so a set digit bit's position is its value. */
static constexpr uint32_t kDigitMask = 0x03FFu;

void TimeDateView::build()
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;
    const bool regular = (sizeClass() == SizeClass::Regular);

    root_.setArea({ 0, 0, (uint16_t)W, (uint16_t)H });
    root_.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(timeAndDate));

    /* Centred date-over-time stack with a small hint below. */
    body_.setAxis(Axis::Column);
    body_.setAlign(Align::Stretch);
    body_.setJustify(Justify::Center);
    body_.setGap(regular ? 6 : 2);
    body_.setGrow(1);

    const fontSize_t big = regular ? FONT_SIZE_12PT : FONT_SIZE_8PT;
    dateLabel_.setFont(big);
    dateLabel_.setAlign(TEXT_ALIGN_CENTER);
    dateLabel_.setColor(Sem::OnSurface);
    dateLabel_.setBasis(regular ? 26 : 16);

    timeLabel_.setFont(big);
    timeLabel_.setAlign(TEXT_ALIGN_CENTER);
    timeLabel_.setColor(Sem::OnSurface);
    timeLabel_.setBasis(regular ? 26 : 16);

    hint_.setFont(FONT_SIZE_6PT);
    hint_.setAlign(TEXT_ALIGN_CENTER);
    hint_.setColor(Sem::OnSurfaceMuted);
    hint_.setText(STR(hintSet));
    hint_.setBasis(12);

    body_.addChild(&dateLabel_);
    body_.addChild(&timeLabel_);
    body_.addChild(&hint_);

    root_.addChild(&bar);
    root_.addChild(&body_);
    screen_.addChild(&root_);
    root_.onLayout();

    showDisplay(state, true);
    screen_.markAllDirty();
}

void TimeDateView::showDisplay(const state_t &s, bool force)
{
    const datetime_t t = utcToLocalTime(s.time, s.settings.utc_timezone);

    char combined[32];
    sniprintf(combined, sizeof(combined), "%02d/%02d/%02d %02d:%02d:%02d",
              t.date, t.month, t.year, t.hour, t.minute, t.second);
    if (!force && (strncmp(combined, dispCache_, sizeof(dispCache_)) == 0))
        return;
    strncpy(dispCache_, combined, sizeof(dispCache_) - 1);
    dispCache_[sizeof(dispCache_) - 1] = '\0';

    sniprintf(dateBuf_, sizeof(dateBuf_), "%02d/%02d/%02d", t.date, t.month,
              t.year);
    sniprintf(timeBuf_, sizeof(timeBuf_), "%02d:%02d:%02d", t.hour, t.minute,
              t.second);
    dateLabel_.setText(dateBuf_);
    timeLabel_.setText(timeBuf_);
    dateLabel_.invalidate();
    timeLabel_.invalidate();
}

void TimeDateView::syncFromState(const state_t &s)
{
    View::syncFromState(s); /* refresh the shared top bar */
    if (!editing_)
        showDisplay(s, false);
}

void TimeDateView::onShow()
{
    /* Never resume mid-edit. */
    if (editing_) {
        editing_ = false;
        showDisplay(state, true);
        hint_.setText(STR(hintSet));
        hint_.invalidate();
    }
}

void TimeDateView::announce()
{
    vpSay(STR(timeAndDate));
}

void TimeDateView::beginEdit()
{
    editing_ = true;
    /* 10 blank digit slots (dd mm yy hh mm); '*' backspaces, digits fill left to
     * right. Seconds are not editable (fixed 00). */
    input_.configureSlots(inputBuf_, sizeof(inputBuf_), "9999999999", '_',
                          TextInput::CursorStyle::Bracket, FONT_SIZE_8PT);
    hint_.setText("dd/mm/yy hh:mm");
    hint_.invalidate();
    refreshEdit();
}

void TimeDateView::refreshEdit()
{
    /* Compose the two labels from the slots, skipping the fixed separators and
     * showing '_' for an un-entered slot. */
    auto d = [&](uint8_t o) -> char {
        const int v = input_.slotDigit(o);
        return (v >= 0) ? static_cast<char>('0' + v) : '_';
    };
    sniprintf(dateBuf_, sizeof(dateBuf_), "%c%c/%c%c/%c%c", d(0), d(1), d(2),
              d(3), d(4), d(5));
    sniprintf(timeBuf_, sizeof(timeBuf_), "%c%c:%c%c:00", d(6), d(7), d(8),
              d(9));
    dateLabel_.setText(dateBuf_);
    timeLabel_.setText(timeBuf_);
    dateLabel_.invalidate();
    timeLabel_.invalidate();
}

void TimeDateView::commit()
{
    /* The user entered a LOCAL time; store the equivalent UTC. */
    auto d = [&](uint8_t o) { return input_.slotDigit(o); };
    datetime_t local = {};
    local.date = static_cast<int8_t>(d(0) * 10 + d(1));
    local.month = static_cast<int8_t>(d(2) * 10 + d(3));
    local.year = static_cast<uint8_t>(d(4) * 10 + d(5));
    local.hour = static_cast<int8_t>(d(6) * 10 + d(7));
    local.minute = static_cast<int8_t>(d(8) * 10 + d(9));
    local.second = 0;
    const datetime_t utc = localTimeToUtc(local, state.settings.utc_timezone);
#ifdef CONFIG_RTC
    /* The screen is only built on targets with a clock to set. */
    platform_setTime(utc);
#endif
    state.time = utc;
    editing_ = false;
    hint_.setText(STR(hintSet));
    hint_.invalidate();
    showDisplay(state, true);
}

NavIntent TimeDateView::onEvent(const Event &e)
{
    if (editing_) {
        if (e.kind != EvKind::Key)
            return NavIntent::none();
        const uint32_t k = e.keys;
        if ((k & KEY_ESC) != 0u) {
            editing_ = false;
            hint_.setText(STR(hintSet));
            hint_.invalidate();
            showDisplay(state, true);
        } else if ((k & KEY_ENTER) != 0u) {
            if (input_.filledSlots() == input_.slotCount())
                commit(); /* only a complete entry is applied */
        } else if (((k & kDigitMask) != 0u) || ((k & KEY_STAR) != 0u)) {
            /* Digit fills the next slot, '*' backspaces -- the shared contract. */
            input_.handleKey(k, 0);
            refreshEdit();
        }
        return NavIntent::none();
    }

    if (e.kind == EvKind::Key) {
        if ((e.keys & KEY_ESC) != 0u)
            return NavIntent::pop();
        if ((e.keys & KEY_ENTER) != 0u) {
            beginEdit();
            return NavIntent::none();
        }
    }

    return NavIntent::none();
}

} // namespace ortxui
