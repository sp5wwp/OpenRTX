/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/SettingsListView.hpp"
#include "base/Event.hpp"
#include "views/UiMemory.hpp"
#include "core/state.h"
#include "core/voicePromptUtils.h"
#include "interfaces/keyboard.h"

#include <cstdio>

namespace ortxui
{

void SettingsListView::buildList(const UiText &title, ListItem *items,
                                 uint8_t count)
{
    rows = items;
    for (uint8_t i = 0; i < count; i++) {
        if (!items[i].checkbox && !items[i].value.isSet())
            items[i].value = valueText[i];
    }

    root.setArea(screenArea());
    root.setAxis(Axis::Column);

    TopBar &bar = useTopBar(title);

    listBox.setItems(items, count);
    listBox.setContentPad(ROW_PAD);
    listBox.setFlag(FLAG_FOCUSABLE, true);
    listBox.setGrow(1);

    root.addChild(&bar);
    root.addChild(&listBox);
    tree.addChild(&root);
    root.onLayout();
    tree.focusFirst();
    tree.markAllDirty();

    for (uint8_t i = 0; i < count; i++) {
        if (rowKind(i) == RowKind::Custom)
            refreshCustom(i, false);
        else if (items[i].value.str() == valueText[i])
            refreshValue(i, false);
    }
}

void SettingsListView::save() const
{
    if (!editing)
        return;

    RowEdit e = {};
    e.row = editRow;
    if (rowKind(editRow) == RowKind::Custom)
        saveCustom(e.custom);
    uiMemory.inlineEdit.keep(id(), e);
}

void SettingsListView::restore()
{
    RowEdit e;
    if (!uiMemory.inlineEdit.take(id(), e))
        return;

    editRow = e.row;
    editing = true;
    if (rowKind(editRow) == RowKind::Custom)
        restoreCustom(e.custom);
    else
        refreshValue(editRow, false);
}

void SettingsListView::announceValue(uint8_t row, const UiText &value,
                                     vpQueueFlags flags)
{
    if ((flags & vpqIncludeDescriptions) != 0) {
        rows[row].label.announce(static_cast<vpQueueFlags>(flags & vpqInit));
        flags = static_cast<vpQueueFlags>(flags & ~vpqInit);
    }
    value.announce(flags);
}

void SettingsListView::speakValue(uint8_t row, const UiText &value)
{
    if (state.settings.vpLevel >= vpLow)
        announceValue(row, value, vp_getVoiceLevelQueueFlags());
}

void SettingsListView::refreshValue(uint8_t row, bool speak)
{
    char inner[VALUE_SIZE - 2];
    formatValue(row, inner, sizeof(inner));

    if (editing && (row == editRow)) {
        sniprintf(valueText[row], VALUE_SIZE, "<%s>", inner);
        if (speak)
            speakValue(row, inner);
    } else {
        sniprintf(valueText[row], VALUE_SIZE, "%s", inner);
    }
    listBox.invalidate();
}

void SettingsListView::beginEdit()
{
    editRow = static_cast<uint8_t>(listBox.selected());
    editing = true;
    onBeginEdit(editRow);
    if (rowKind(editRow) == RowKind::Custom)
        refreshCustom(editRow, true);
    else
        refreshValue(editRow);
}

void SettingsListView::endEdit()
{
    editing = false;
    if (rowKind(editRow) == RowKind::Custom)
        refreshCustom(editRow, false);
    else
        refreshValue(editRow, false);
}

NavIntent SettingsListView::onEvent(const Event &e)
{
    if (editing) {
        /* Custom rows own their whole editing key stream. */
        if (onEditEvent(e))
            return NavIntent::none();

        int8_t dir = 0;
        if (e.kind == EvKind::Encoder) {
            dir = e.encoder;
        } else if ((e.kind == EvKind::Key) || (e.kind == EvKind::KeyRepeat)) {
            /* A held arrow keeps adjusting; ENTER and ESC act once. */
            const bool repeat = (e.kind == EvKind::KeyRepeat);
            if (!repeat && ((e.keys & (KEY_ENTER | KEY_ESC)) != 0u)) {
                endEdit();
                return NavIntent::none();
            }
            if ((e.keys & (KEY_UP | KEY_RIGHT)) != 0u)
                dir = +1;
            else if ((e.keys & (KEY_DOWN | KEY_LEFT)) != 0u)
                dir = -1;
        }
        if (dir != 0) {
            onAdjust(editRow, dir);
            refreshValue(editRow);
        }
        return NavIntent::none(); /* swallow all input while editing */
    }

    if (e.kind == EvKind::Key) {
        if ((e.keys & KEY_ESC) != 0u)
            return NavIntent::pop();
        if ((e.keys & KEY_ENTER) != 0u) {
            const uint8_t sel = static_cast<uint8_t>(listBox.selected());
            switch (rowKind(sel)) {
                case RowKind::Checkbox: {
                    ListItem &it = rows[sel];
                    it.checked = !it.checked;
                    onToggle(sel, it.checked);
                    listBox.invalidate();
                    speakValue(sel, it.checked ? STR(on) : STR(off));
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

    tree.dispatch(e);
    return NavIntent::none();
}

} // namespace ortxui
