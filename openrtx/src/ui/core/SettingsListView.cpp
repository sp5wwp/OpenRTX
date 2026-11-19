/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/SettingsListView.hpp"
#include "core/Event.hpp"
#include "core/UiMemory.hpp"
#include "core/state.h"
#include "core/voicePromptUtils.h"
#include "interfaces/keyboard.h"
#include "hwconfig.h"

#include <cstdio>

namespace ortxui
{

void SettingsListView::buildList(const UiText &title, ListItem *items,
                                 uint8_t count)
{
    rows_ = items;
    for (uint8_t i = 0; i < count; i++) {
        if (!items[i].checkbox && !items[i].value.isSet())
            items[i].value = values_[i];
    }

    root_.setArea({ 0, 0, static_cast<uint16_t>(CONFIG_SCREEN_WIDTH),
                    static_cast<uint16_t>(CONFIG_SCREEN_HEIGHT) });
    root_.setAxis(Axis::Column);

    TopBar &bar = useTopBar(title);

    list_.setItems(items, count);
    list_.setContentPad(ROW_PAD);
    list_.setFlag(FLAG_FOCUSABLE, true);
    list_.setGrow(1);

    root_.addChild(&bar);
    root_.addChild(&list_);
    screen_.addChild(&root_);
    root_.onLayout();
    screen_.focusFirst();
    screen_.markAllDirty();

    for (uint8_t i = 0; i < count; i++) {
        if (rowKind(i) == RowKind::Custom)
            refreshCustom(i, false);
        else if (items[i].value.str() == values_[i])
            refreshValue(i, false);
    }
}

void SettingsListView::save() const
{
    InlineEdit &ie = uiMemory.inlineEdit;
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

void SettingsListView::restore()
{
    InlineEdit &ie = uiMemory.inlineEdit;
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

void SettingsListView::announceValue(uint8_t row, const UiText &value,
                                     vpQueueFlags flags)
{
    if ((flags & vpqIncludeDescriptions) != 0) {
        rows_[row].label.announce(static_cast<vpQueueFlags>(flags & vpqInit));
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

    if (editing_ && (row == editRow_)) {
        sniprintf(values_[row], VALUE_SIZE, "<%s>", inner);
        if (speak)
            speakValue(row, inner);
    } else {
        sniprintf(values_[row], VALUE_SIZE, "%s", inner);
    }
    list_.invalidate();
}

void SettingsListView::beginEdit()
{
    editRow_ = static_cast<uint8_t>(list_.selected());
    editing_ = true;
    onBeginEdit(editRow_);
    if (rowKind(editRow_) == RowKind::Custom)
        refreshCustom(editRow_, true);
    else
        refreshValue(editRow_);
}

void SettingsListView::endEdit()
{
    editing_ = false;
    if (rowKind(editRow_) == RowKind::Custom)
        refreshCustom(editRow_, false);
    else
        refreshValue(editRow_, false);
}

NavIntent SettingsListView::onEvent(const Event &e)
{
    if (editing_) {
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
            onAdjust(editRow_, dir);
            refreshValue(editRow_);
        }
        return NavIntent::none(); /* swallow all input while editing */
    }

    if (e.kind == EvKind::Key) {
        if ((e.keys & KEY_ESC) != 0u)
            return NavIntent::pop();
        if ((e.keys & KEY_ENTER) != 0u) {
            const uint8_t sel = static_cast<uint8_t>(list_.selected());
            switch (rowKind(sel)) {
                case RowKind::Checkbox: {
                    ListItem &it = rows_[sel];
                    it.checked = !it.checked;
                    onToggle(sel, it.checked);
                    list_.invalidate();
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

    screen_.dispatch(e);
    return NavIntent::none();
}

} // namespace ortxui
