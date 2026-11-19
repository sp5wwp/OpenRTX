/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/TopBar.hpp"
#include "core/graphics.h"
#include "core/messages.h"

#include <cstdio>

namespace ortxui
{

/** Left-slot glyphs, indexed by locked (bit 0) and unread messages (bit 1). */
static constexpr const char *LEFT_GLYPHS[] = {
    "",
    SYMBOL_LOCK,
    SYMBOL_ENVELOPE,
    SYMBOL_LOCK SYMBOL_ENVELOPE,
};

TopBar::TopBar()
{
    const bool regular = (sizeClass() == SizeClass::Regular);
    const int16_t barH = regular ? 16 : 12;
    const fontSize_t font = FONT_SIZE_6PT;

    setAxis(Axis::Row);
    setAlign(Align::Center);
    setBackground(Sem::Surface);
    setBasis(barH);
    setPadding(4, 0);
    setGap(4);

    /* Far-left keypad-lock slot: the glyph while locked, blank otherwise. Its
     * fixed width also balances the right status cluster, keeping the centred
     * title/clock centred. */
    lock_.setFont(font);
    lock_.setAlign(TEXT_ALIGN_LEFT);
    lock_.setColor(Sem::Accent);
    lock_.setText("");
    lock_.setBasis(regular ? 28 : 22);

    /* Centre: page title, or the clock when there is no title. */
    middle_.setFont(font);
    middle_.setAlign(TEXT_ALIGN_CENTER);
    middle_.setColor(Sem::OnSurface);
    middle_.setGrow(1);

    /* Right cluster: battery as percentage or icon. */

    pct_.setFont(font);
    pct_.setAlign(TEXT_ALIGN_RIGHT);
    pct_.setColor(Sem::OnSurface);
    pct_.setText(pctBuf_);
    pct_.setBasis(regular ? 28 : 24);

    /* Upright battery glyph: a narrow slot, cross-centred at its natural size. */
    battery_.setBasis(regular ? 16 : 12);
    battery_.setAlignSelf(Align::Center);

    addChild(&lock_);
    addChild(&middle_);
    addChild(&pct_);
    addChild(&battery_);
}

TopBar::~TopBar()
{
}

void TopBar::reset(const UiText &title)
{
    clockBuf_[0] = '\0';
    pctBuf_[0] = '\0';
    lastMinute_ = -1;
    lastCharge_ = 0xFFu;
    lastLocked_ = false;
    lastUnread_ = false;
    lastShowIcon_ = -1;
    lock_.setText("");
    pct_.setFlag(FLAG_HIDDEN, false);
    battery_.setFlag(FLAG_HIDDEN, false);

    titleStr_ = title;
    hasTitle_ = !titleStr_.empty();
    middle_.setText(hasTitle_ ? titleStr_ : UiText(clockBuf_));
}

void TopBar::setTitle(const UiText &t)
{
    titleStr_ = t;
    hasTitle_ = !titleStr_.empty();
    middle_.setText(hasTitle_ ? titleStr_ : UiText(clockBuf_));
    middle_.invalidate();
}

void TopBar::update(const state_t &s)
{
    /* The clock occupies the centre only when the screen has no title. */
    if (!hasTitle_ && (s.time.minute != lastMinute_)) {
        snprintf(clockBuf_, sizeof(clockBuf_), "%02u:%02u", s.time.hour,
                 s.time.minute);
        middle_.setText(clockBuf_);
        middle_.invalidate();
        lastMinute_ = s.time.minute;
    }

    if (s.charge != lastCharge_) {
        snprintf(pctBuf_, sizeof(pctBuf_), "%u%%", s.charge);
        pct_.invalidate();
        battery_.setCharge(s.charge);
        battery_.invalidate();
        lastCharge_ = s.charge;
    }

    bool relayout = false;

    /* Battery: percentage OR icon, per settings.showBatteryIcon. */
    const int8_t showIcon = s.settings.showBatteryIcon ? 1 : 0;
    if (showIcon != lastShowIcon_) {
        pct_.setFlag(FLAG_HIDDEN, showIcon != 0);
        battery_.setFlag(FLAG_HIDDEN, showIcon == 0);
        lastShowIcon_ = showIcon;
        relayout = true;
    }

#ifdef CONFIG_MESSAGES
    const bool unread = (messages_countUnread() > 0);
#else
    const bool unread = false;
#endif
    if ((s.keypad_locked != lastLocked_) || (unread != lastUnread_)) {
        /* Constant-width slot, so just swap the glyphs — no re-flow needed. */
        const uint8_t idx = (s.keypad_locked ? 1u : 0u) | (unread ? 2u : 0u);
        lock_.setText(LEFT_GLYPHS[idx]);
        lock_.invalidate();
        lastLocked_ = s.keypad_locked;
        lastUnread_ = unread;
    }

    if (relayout) {
        onLayout();
        invalidate();
    }
}

} // namespace ortxui
