/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/ChecklistView.hpp"
#include "core/Event.hpp"
#include "interfaces/keyboard.h"
#include "hwconfig.h"

namespace ortxui
{

void ChecklistView::build()
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;

    static const UiText kLabels[RowCount] = {
        STR(phonetic),
        STR(macroLatching),
    };

    root_.setArea({ 0, 0, (uint16_t)W, (uint16_t)H });
    root_.setAxis(Axis::Column);

    topBar_.init(STR(accessibility));
    setTopBar(&topBar_);

    for (uint8_t i = 0; i < RowCount; i++) {
        items_[i].label = kLabels[i];
        items_[i].checkbox = true;
    }

    list_.setItems(items_, RowCount);
    list_.setContentPad(4);
    list_.setFlag(FLAG_FOCUSABLE, true);
    list_.setGrow(1);

    root_.addChild(&topBar_);
    root_.addChild(&list_);
    screen_.addChild(&root_);
    root_.onLayout();
    screen_.focusFirst();

    screen_.markAllDirty();
}

void ChecklistView::syncFromState(const state_t &s)
{
    View::syncFromState(s); /* top bar */

    if (seeded_)
        return;

    /* Seed the ticks from the matching settings flags (once). */
    const settings_t &st = s.settings;
    items_[RowPhonetic].checked = (st.vpPhoneticSpell != 0u);
    items_[RowLatch].checked = (st.macroMenuLatch != 0u);

    seeded_ = true;
    list_.invalidate();
}

NavIntent ChecklistView::onEvent(const Event &e)
{
    if (e.kind == EvKind::Key) {
        if ((e.keys & KEY_ESC) != 0u)
            return NavIntent::pop();

        if ((e.keys & KEY_ENTER) != 0u) {
            ListItem *it = list_.selectedItem();
            if ((it != nullptr) && it->checkbox) {
                it->checked = !it->checked;
                applyToggle(list_.selected(), it->checked);
                list_.invalidate();
                /* toggle: no nav change */
                vpSay(it->checked ? STR(on) : STR(off));
            }
            return NavIntent::none();
        }
    }

    screen_.dispatch(e);
    return NavIntent::none();
}

void ChecklistView::applyToggle(uint16_t row, bool on)
{
    /* Write straight into the live settings, matching how the classic UI
     * mutates state.settings in the FSM (same UI thread; persisted to flash at
     * shutdown by state_terminate()). syncFromState only seeds once, so it
     * won't fight these edits. */
    switch (row) {
        case RowPhonetic:
            state.settings.vpPhoneticSpell = on ? 1u : 0u;
            break;
        case RowLatch:
            state.settings.macroMenuLatch = on ? 1u : 0u;
            break;
        default:
            break;
    }
}

} // namespace ortxui
