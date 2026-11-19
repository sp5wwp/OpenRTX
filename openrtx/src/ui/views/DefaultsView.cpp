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

/** Space between the prompt and the hint. */
static constexpr uint8_t LINE_GAP = 6;

/** Height of the prompt line. */
static constexpr int16_t PROMPT_H = (sizeClass() == SizeClass::Regular) ? 18 :
                                                                          14;

/** Height of the hint line. */
static constexpr int16_t HINT_H = 12;

void DefaultsView::build()
{
    const bool regular = (sizeClass() == SizeClass::Regular);

    root_.setArea({ 0, 0, CONFIG_SCREEN_WIDTH, CONFIG_SCREEN_HEIGHT });
    root_.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(defaultSettings));

    /* Two single-line labels, centred as one block. */
    body_.setAxis(Axis::Column);
    body_.setAlign(Align::Stretch);
    body_.setJustify(Justify::Center);
    body_.setGap(LINE_GAP);
    body_.setGrow(1);

    prompt_.setFont(regular ? FONT_SIZE_10PT : FONT_SIZE_8PT);
    prompt_.setAlign(TEXT_ALIGN_CENTER);
    prompt_.setColor(Sem::OnSurface);
    prompt_.setBasis(PROMPT_H);

    hint_.setFont(FONT_SIZE_6PT);
    hint_.setAlign(TEXT_ALIGN_CENTER);
    hint_.setColor(Sem::OnSurfaceMuted);
    hint_.setBasis(HINT_H);

    updateText();

    body_.addChild(&prompt_);
    body_.addChild(&hint_);

    root_.addChild(&bar);
    root_.addChild(&body_);
    screen_.addChild(&root_);
    root_.onLayout();
}

void DefaultsView::updateText()
{
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

void DefaultsView::announce()
{
    vpSay(prompt_.text());
}

NavIntent DefaultsView::onEvent(const Event &e)
{
    if (e.kind == EvKind::Key) {
        if ((e.keys & KEY_ESC) != 0u)
            return NavIntent::pop();
        if ((e.keys & KEY_ENTER) != 0u) {
            if (!armed_) {
                armed_ = true;
                updateText();
                vpSay(STR(areYouSure));
                return NavIntent::none();
            }
            state_resetSettingsAndVfo();
            return NavIntent::pop();
        }
    }

    return NavIntent::none();
}

} // namespace ortxui
