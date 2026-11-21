/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/MessagesView.hpp"
#include "views/MessageDetailView.hpp"
#include "views/MessageComposeView.hpp"
#include "base/Event.hpp"
#include "views/UiMemory.hpp"
#include "core/state.h"
#include "core/graphics.h"
#include "core/utils.h"
#include "style/Symbols.hpp"
#include "interfaces/keyboard.h"
#include "rtx/rtx.h"
#include "hwconfig.h"

#include <cstdio>

namespace ortxui
{

void MessagesView::build()
{
    const bool regular = (sizeClass() >= SizeClass::Regular);

    root.setArea({ 0, 0, CONFIG_SCREEN_WIDTH, CONFIG_SCREEN_HEIGHT });
    root.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(messages));

    listBox.setContentPad(1);
    listBox.setFlag(FlagFocusable, true);
    listBox.setGrow(1);

    emptyLabel.setFont(regular ? FONT_SIZE_10PT : FONT_SIZE_8PT);
    emptyLabel.setAlign(TEXT_ALIGN_CENTER);
    emptyLabel.setColor(Sem::OnSurfaceMuted);
    emptyLabel.setText(STR(noMessages));
    emptyLabel.setGrow(1);

    root.addChild(&bar);
    root.addChild(&listBox);
    root.addChild(&emptyLabel);
    tree.addChild(&root);
}

void MessagesView::rowAt(uint16_t index, ListItem &out) const
{
    const struct message *msg = messages_get(index);
    if (msg == nullptr) {
        out.label = (index == count) ? STR(newMessage) : UiText("");
        return;
    }

    const char *other = (msg->direction == MSG_DIR_TX) ? msg->recipient :
                                                         msg->sender;
    const char *mark = "";
    if (msg->status == MSG_STATUS_FAILED)
        mark = SYMBOL_WARNING " ";
    else if (msg->unread)
        mark = SYMBOL_ENVELOPE " ";

    sniprintf(label, sizeof(label), "%s%s", mark, other);
    out.label = label;
    out.value = msg->body;
}

uint32_t MessagesView::digest(uint8_t mode) const
{
    /* FNV-1a over what the rows show; the inbox holds a few dozen entries,
     * so walking it once per frame is cheap. */
    const size_t n = messages_count();
    uint32_t h = FNV1A_BASIS;
    h = fnv1a(h, static_cast<uint32_t>(n));
    h = fnv1a(h, messages_canCompose(mode) ? 1u : 0u);
    for (size_t i = 0; i < n; i++) {
        const struct message *msg = messages_get(i);
        const uint32_t flags = (static_cast<uint32_t>(msg->status) << 1)
                             | msg->unread;
        h = fnv1a(h, msg->sequence);
        h = fnv1a(h, flags);
    }
    return h;
}

uint16_t MessagesView::pinnedRow() const
{
    if (pinned == NEW_ROW)
        return canCompose ? count : 0;

    const size_t idx = messages_findBySequence(pinned);
    return (idx < count) ? static_cast<uint16_t>(idx) : 0;
}

void MessagesView::pin()
{
    const uint16_t sel = listBox.selected();
    const struct message *msg = messages_get(sel);
    if (msg != nullptr)
        pinned = msg->sequence;
    else
        pinned = (canCompose && (sel == count)) ? NEW_ROW : 0;
}

void MessagesView::reload(uint8_t mode)
{
    count = static_cast<uint16_t>(messages_count());
    canCompose = messages_canCompose(mode);
    shownDigest = digest(mode);

    const bool empty = (rowCount() == 0);
    listBox.setModel(this);
    listBox.setSelected(pinnedRow(), false);
    listBox.setFlag(FlagHidden, empty);
    emptyLabel.setFlag(FlagHidden, !empty);

    root.onLayout();
    if (!empty)
        tree.focusFirst();
    tree.markAllDirty();
}

void MessagesView::save() const
{
    uiMemory.inboxPinned = pinned;
}

void MessagesView::restore()
{
    /* Back from the detail view this is the message it ended on. */
    pinned = uiMemory.inboxPinned;
}

void MessagesView::onShow()
{
    reload(state.channel.mode);
}

void MessagesView::syncFromState(const state_t &s)
{
    View::syncFromState(s);

    if (digest(s.channel.mode) != shownDigest)
        reload(s.channel.mode);
}

NavIntent MessagesView::onEvent(const Event &e)
{
    if (e.kind == EvKind::Key) {
        if ((e.keys & KEY_ESC) != 0u) {
            /* Unpin, so the next visit starts at the newest message. */
            pinned = 0;
            return NavIntent::pop();
        }

        if ((e.keys & KEY_ENTER) != 0u) {
            const uint16_t sel = listBox.selected();
            const struct message *msg = messages_get(sel);
            if (msg != nullptr) {
                pinned = msg->sequence;
                MessageDetailView::open(msg->sequence);
                return NavIntent::push(ViewId::MessageDetail);
            }

            if (canCompose && (sel == count)) {
                /* On M17 the channel destination is the natural default. */
                const char *dest = (state.channel.mode == OPMODE_M17) ?
                                       state.settings.m17_dest :
                                       nullptr;
                if (MessageComposeView::open(dest, false))
                    return NavIntent::push(ViewId::MessageCompose);
            }
            return NavIntent::none();
        }
    }

    tree.dispatch(e);
    pin();
    return NavIntent::none();
}

} // namespace ortxui
