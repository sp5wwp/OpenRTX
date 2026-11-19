/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/SettingsListView.hpp"
#include "core/Event.hpp"
#include "core/UiMemory.hpp"
#include "interfaces/keyboard.h"
#include "hwconfig.h"

#include <cstdio>

namespace ortxui
{

void SettingsListView::buildList(const UiText &title, ListItem *items,
                                 uint8_t count)
{
    root_.setArea({ 0, 0, (uint16_t)CONFIG_SCREEN_WIDTH,
                    (uint16_t)CONFIG_SCREEN_HEIGHT });
    root_.setAxis(Axis::Column);

    TopBar &bar = useTopBar(title);

    list_.setItems(items, count);
    list_.setContentPad(4);
    list_.setFlag(FLAG_FOCUSABLE, true);
    list_.setGrow(1);

    root_.addChild(&bar);
    root_.addChild(&list_);
    screen_.addChild(&root_);
    root_.onLayout();
    screen_.focusFirst();
    screen_.markAllDirty();
}

void SettingsListView::save(UiMemory &m) const
{
    InlineEdit &ie = m.inlineEdit;
    if (!editing_) {
        if (ie.owner == id())
            ie.owner = ViewId::Count;
        return;
    }

    ie.list.row = editRow_;
    if (rowKind(editRow_) == RowKind::Custom)
        saveCustom(ie.list);
    ie.owner = id();
}

void SettingsListView::restore(UiMemory &m)
{
    InlineEdit &ie = m.inlineEdit;
    if (ie.owner != id())
        return;
    ie.owner = ViewId::Count;

    editRow_ = ie.list.row;
    editing_ = true;
    if (rowKind(editRow_) == RowKind::Custom)
        restoreCustom(ie.list);
    else
        refreshValue(editRow_, false);
}

void SettingsListView::refreshValue(uint8_t row, bool speak)
{
    char inner[32];
    formatValue(row, inner, sizeof(inner));

    /* The row being edited shows its value in <angle brackets> and speaks it per
     * step (the label was announced on navigation). */
    if (editing_ && (row == editRow_)) {
        char wrapped[40];
        snprintf(wrapped, sizeof(wrapped), "<%s>", inner);
        setValueText(row, wrapped);
        if (speak)
            vpSay(inner);
    } else {
        setValueText(row, inner);
    }
    list_.invalidate();
}

void SettingsListView::beginEdit()
{
    editRow_ = (uint8_t)list_.selected();
    editing_ = true;
    onBeginEdit(editRow_);
    if (rowKind(editRow_) == RowKind::Custom)
        refreshCustom(editRow_);
    else
        refreshValue(editRow_);
    list_.invalidate();
}

void SettingsListView::endEdit()
{
    editing_ = false;
    if (rowKind(editRow_) == RowKind::Custom)
        refreshCustom(editRow_);
    else
        refreshValue(editRow_);
    list_.invalidate();
}

NavIntent SettingsListView::onEvent(const Event &e)
{
    if (editing_) {
        /* Custom rows own their whole editing key stream. */
        if (onEditEvent(e))
            return NavIntent::none();

        int dir = 0;
        if (e.kind == EvKind::Encoder) {
            dir = e.encoder;
        } else if (e.kind == EvKind::Key) {
            if ((e.keys & (KEY_ENTER | KEY_ESC)) != 0u) {
                endEdit();
                return NavIntent::none();
            }
            if ((e.keys & (KEY_UP | KEY_RIGHT)) != 0u)
                dir = +1;
            else if ((e.keys & (KEY_DOWN | KEY_LEFT)) != 0u)
                dir = -1;
        }
        if (dir != 0) {
            onAdjust(editRow_, dir);
            refreshValue(editRow_);
        }
        return NavIntent::none(); /* swallow all input while editing */
    }

    if (e.kind == EvKind::Key) {
        if ((e.keys & KEY_ESC) != 0u)
            return NavIntent::pop();
        if ((e.keys & KEY_ENTER) != 0u) {
            const uint8_t sel = (uint8_t)list_.selected();
            switch (rowKind(sel)) {
                case RowKind::Checkbox: {
                    ListItem *it = list_.selectedItem();
                    if (it != nullptr) {
                        it->checked = !it->checked;
                        onToggle(sel, it->checked);
                        list_.invalidate();
                        vpSay(it->checked ? STR(on) : STR(off));
                    }
                    return NavIntent::none();
                }
                case RowKind::Action:
                    return onActivate(sel);
                default: /* Stepper, Custom */
                    beginEdit();
                    return NavIntent::none();
            }
        }
    }

    screen_.dispatch(e);
    return NavIntent::none();
}

} // namespace ortxui
