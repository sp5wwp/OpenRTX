/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/PickerView.hpp"
#include "core/UiMemory.hpp"
#include "core/Event.hpp"
#include "interfaces/keyboard.h"
#include "hwconfig.h"

namespace ortxui
{

void PickerView::build()
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;
    grid_.setArea({ 0, 0, static_cast<uint16_t>(W), static_cast<uint16_t>(H) });
    grid_.setPosition(uiMemory.edit.pickerSelected, uiMemory.edit.pickerTop);
    screen_.addChild(&grid_);
    screen_.markAllDirty();
}

void PickerView::open()
{
    uiMemory.edit.pickerSelected = 0;
    uiMemory.edit.pickerTop = 0;
    uiMemory.edit.picked = 0;
}

void PickerView::save(UiMemory &m) const
{
    m.edit.pickerSelected = static_cast<uint8_t>(grid_.selected());
    m.edit.pickerTop = static_cast<uint8_t>(grid_.top());
}

void PickerView::announce()
{
    vpSay(STR(pickCharacter));
}

NavIntent PickerView::onEvent(const Event &e)
{
    if (e.kind == EvKind::Encoder) {
        grid_.move(e.encoder);
        return NavIntent::none();
    }
    /* A held arrow keeps moving; ENTER and ESC act once. */
    const bool repeat = (e.kind == EvKind::KeyRepeat);
    if ((e.kind != EvKind::Key) && !repeat)
        return NavIntent::none();

    const uint32_t k = e.keys;
    const int c = static_cast<int>(grid_.cols());
    if (!repeat && ((k & KEY_ENTER) != 0u)) {
        uiMemory.edit.picked = grid_.selectedCodePoint();
        return NavIntent::pop();
    }
    if (!repeat && ((k & KEY_ESC) != 0u))
        return NavIntent::pop();
    if ((k & KEY_LEFT) != 0u)
        grid_.move(-1);
    else if ((k & KEY_RIGHT) != 0u)
        grid_.move(+1);
    else if ((k & KEY_UP) != 0u)
        grid_.move(-c);
    else if ((k & KEY_DOWN) != 0u)
        grid_.move(+c);

    return NavIntent::none();
}

} // namespace ortxui
