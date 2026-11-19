/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/DefaultsView.hpp"
#include "core/Event.hpp"
#include "interfaces/keyboard.h"
#include "hwconfig.h"

namespace ortxui
{

void DefaultsView::build()
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;
    const bool regular = (sizeClass() == SizeClass::Regular);

    root_.setArea({ 0, 0, (uint16_t)W, (uint16_t)H });
    root_.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(defaultSettings));

    /* Centred prompt + hint stack (Label is single-line, so two of them). */
    body_.setAxis(Axis::Column);
    body_.setAlign(Align::Stretch);
    body_.setJustify(Justify::Center);
    body_.setGap(6);
    body_.setGrow(1);

    prompt_.setFont(regular ? FONT_SIZE_10PT : FONT_SIZE_8PT);
    prompt_.setAlign(TEXT_ALIGN_CENTER);
    prompt_.setColor(Sem::OnSurface);
    prompt_.setBasis(regular ? 18 : 14);

    hint_.setFont(FONT_SIZE_6PT);
    hint_.setAlign(TEXT_ALIGN_CENTER);
    hint_.setColor(Sem::OnSurfaceMuted);
    hint_.setBasis(12);

    updateText();

    body_.addChild(&prompt_);
    body_.addChild(&hint_);

    root_.addChild(&bar);
    root_.addChild(&body_);
    screen_.addChild(&root_);
    root_.onLayout();
    screen_.markAllDirty();
}

void DefaultsView::updateText()
{
    /* Keep both lines within the 160px width (no wrap/clip). */
    if (armed_) {
        prompt_.setText(STR(areYouSure));
        hint_.setText(STR(hintConfirmReset));
    } else {
        prompt_.setText(STR(resetSettings));
        hint_.setText(STR(hintOkCancel));
    }
    prompt_.invalidate();
    hint_.invalidate();
}

void DefaultsView::onShow()
{
    /* Never resume in the armed state. */
    if (armed_) {
        armed_ = false;
        updateText();
    }
}

void DefaultsView::announce()
{
    vpSay(prompt_.text());
}

NavIntent DefaultsView::onEvent(const Event &e)
{
    if (e.kind == EvKind::Key) {
        if ((e.keys & KEY_ESC) != 0u) {
            armed_ = false;
            return NavIntent::pop();
        }
        if ((e.keys & KEY_ENTER) != 0u) {
            if (!armed_) {
                armed_ = true;
                updateText();
                vpSay(STR(areYouSure));
                return NavIntent::none();
            }
            state_resetSettingsAndVfo();
            armed_ = false;
            return NavIntent::pop();
        }
    }

    return NavIntent::none();
}

} // namespace ortxui
