/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/TopBar.hpp"
#include "core/Layout.hpp"
#include "core/UiMemory.hpp"
#include "core/graphics.h"
#include "core/messages.h"
#include "render/DrawCtx.hpp"
#include "style/Symbols.hpp"
#include "widgets/BatteryIcon.hpp"

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

/* Whether the bar is laid out for the roomy screen. */
static constexpr bool REGULAR = (sizeClass() == SizeClass::Regular);

/* Bar height. */
static constexpr int16_t BAR_H = REGULAR ? 16 : 12;

/* Inset of the slots from the bar ends, and the gap between slots. */
static constexpr int16_t PAD_X = 4;
static constexpr int16_t GAP = 4;

/* Width of the left slot. Fixed, it also balances the right slot, keeping the
 * centred title or clock centred. */
static constexpr int16_t LOCK_W = REGULAR ? 28 : 22;

/* Width of the battery percentage and of the battery icon slots. */
static constexpr int16_t PCT_W = REGULAR ? 28 : 24;
static constexpr int16_t ICON_W = REGULAR ? 16 : 12;

/* Text font of the bar. */
static constexpr fontSize_t FONT = FONT_SIZE_6PT;

/**
 * Paint a text in its box, clipped to it.
 *
 * @param d: drawing context.
 * @param box: text box.
 * @param align: horizontal alignment.
 * @param color: text colour.
 * @param text: text.
 * @param ellipsize: end a text too wide for the box with an ellipsis.
 */
static void paintText(DrawCtx &d, const Rect &box, textAlign_t align, Sem color,
                      const char *text, bool ellipsize = false)
{
    const bool clip = !box.empty();
    if (clip)
        d.pushClip(box);
    if (ellipsize)
        d.textInBoxEllipsized(box, FONT, align, color, text);
    else
        d.textInBox(box, FONT, align, color, text);
    if (clip)
        d.popClip();
}

TopBar::TopBar()
{
    setBasis(BAR_H);
}

void TopBar::reset(const UiText &title)
{
    title_ = title;
    hasTitle_ = !title_.empty();
    minute_ = -1;
    charge_ = 0xFF;
    glyphs_ = 0;
    showIcon_ = state.settings.showBatteryIcon;
}

const char *TopBar::middleText(char *clock) const
{
    if (hasTitle_)
        return title_.str();

    clock[0] = '\0';
    if (minute_ >= 0)
        sniprintf(clock, CLOCK_SIZE, "%02u:%02u", static_cast<unsigned>(hour_),
                  static_cast<unsigned>(minute_));
    return clock;
}

int16_t TopBar::rightWidth() const
{
    return showIcon_ ? ICON_W : PCT_W;
}

void TopBar::update(const state_t &s)
{
    /* The clock occupies the centre only when the screen has no title. */
    if (!hasTitle_ && (s.time.minute != minute_)) {
        hour_ = s.time.hour;
        minute_ = static_cast<int8_t>(s.time.minute);
        invalidate();
    }

    if (s.charge != charge_) {
        charge_ = s.charge;
        invalidate();
    }

    /* Battery: percentage or icon, per settings.showBatteryIcon. */
    const bool showIcon = s.settings.showBatteryIcon;
    const bool relayout = (showIcon != showIcon_);
    showIcon_ = showIcon;

#ifdef CONFIG_MESSAGES
    const bool unread = (messages_countUnread() > 0);
#else
    const bool unread = false;
#endif
    const uint8_t glyphs = (uiMemory.keypadLocked ? 1u : 0u)
                         | (unread ? 2u : 0u);
    if (glyphs != glyphs_) {
        /* Constant-width slot, so just swap the glyphs — no re-flow needed. */
        glyphs_ = glyphs;
        invalidate();
    }

    if (relayout) {
        onLayout();
        invalidate();
    }
}

void TopBar::onLayout()
{
    /* A padded row: the left and right slots at their fixed widths, the
     * centre taking the rest. A bar with no room keeps the slots it had. */
    const int cw = static_cast<int>(area_.w) - 2 * PAD_X;
    const int ch = static_cast<int>(area_.h);
    if ((cw <= 0) || (ch <= 0))
        return;

    const int middle = cw - LOCK_W - rightWidth() - 2 * GAP;
    middleW_ = static_cast<uint16_t>((middle > 0) ? middle : 0);

    /* The text slots are centred at their line height. */
    const int lineH = gfx_getFontLineHeight(FONT);
    textH_ = static_cast<uint8_t>((lineH < ch) ? lineH : ch);
}

void TopBar::draw(DrawCtx &d)
{
    d.fillRect(area_, Sem::Surface);

    const int16_t textY = static_cast<int16_t>(
        area_.y + (static_cast<int>(area_.h) - textH_) / 2);
    int16_t x = static_cast<int16_t>(area_.x + PAD_X);

    paintText(d, { x, textY, static_cast<uint16_t>(LOCK_W), textH_ },
              TEXT_ALIGN_LEFT, Sem::Accent, LEFT_GLYPHS[glyphs_]);

    /* A text too wide for the centre also takes the left slot when that shows
     * no glyph, and ellipsizes if it still does not fit. */
    char clock[CLOCK_SIZE];
    const char *middle = middleText(clock);
    Rect box = { static_cast<int16_t>(x + LOCK_W + GAP), textY, middleW_,
                 textH_ };
    if ((glyphs_ == 0) && (gfx_getTextWidth(FONT, middle) > middleW_)) {
        box.x = x;
        box.w = static_cast<uint16_t>(middleW_ + LOCK_W + GAP);
    }
    paintText(d, box, TEXT_ALIGN_CENTER, Sem::OnSurface, middle, true);
    x = static_cast<int16_t>(x + LOCK_W + GAP + middleW_ + GAP);

    if (!showIcon_) {
        char pct[8] = "";
        if (charge_ != 0xFF)
            sniprintf(pct, sizeof(pct), "%u%%", charge_);
        paintText(d, { x, textY, static_cast<uint16_t>(PCT_W), textH_ },
                  TEXT_ALIGN_RIGHT, Sem::OnSurface, pct);
    } else {
        /* The icon is centred across the bar at its own height. */
        const Size icon = BatteryIcon::size();
        const uint16_t iconH = (icon.h < area_.h) ? icon.h : area_.h;
        const Rect box = {
            x, static_cast<int16_t>(area_.y + (area_.h - iconH) / 2),
            static_cast<uint16_t>(ICON_W), iconH
        };
        const bool clip = !box.empty();
        if (clip)
            d.pushClip(box);
        BatteryIcon::paint(d, box, charge_);
        if (clip)
            d.popClip();
    }
}

} // namespace ortxui
