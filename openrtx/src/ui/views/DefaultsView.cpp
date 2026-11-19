/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/DefaultsView.hpp"
#include "base/Event.hpp"
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

    root.setArea({ 0, 0, CONFIG_SCREEN_WIDTH, CONFIG_SCREEN_HEIGHT });
    root.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(defaultSettings));

    /* Two single-line labels, centred as one block. */
    body.setAxis(Axis::Column);
    body.setAlign(Align::Stretch);
    body.setJustify(Justify::Center);
    body.setGap(LINE_GAP);
    body.setGrow(1);

    prompt.setFont(regular ? FONT_SIZE_10PT : FONT_SIZE_8PT);
    prompt.setAlign(TEXT_ALIGN_CENTER);
    prompt.setColor(Sem::OnSurface);
    prompt.setBasis(PROMPT_H);

    hint.setFont(FONT_SIZE_6PT);
    hint.setAlign(TEXT_ALIGN_CENTER);
    hint.setColor(Sem::OnSurfaceMuted);
    hint.setBasis(HINT_H);

    updateText();

    body.addChild(&prompt);
    body.addChild(&hint);

    root.addChild(&bar);
    root.addChild(&body);
    tree.addChild(&root);
    root.onLayout();
}

void DefaultsView::updateText()
{
    if (armed) {
        prompt.setText(STR(areYouSure));
        hint.setText(STR(hintConfirmReset));
    } else {
        prompt.setText(STR(resetSettings));
        hint.setText(STR(hintOkCancel));
    }
    prompt.invalidate();
    hint.invalidate();
}

void DefaultsView::announce()
{
    vpSay(prompt.text());
}

NavIntent DefaultsView::onEvent(const Event &e)
{
    if (e.kind == EvKind::Key) {
        if ((e.keys & KEY_ESC) != 0u)
            return NavIntent::pop();
        if ((e.keys & KEY_ENTER) != 0u) {
            if (!armed) {
                armed = true;
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
