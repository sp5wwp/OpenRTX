/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/MessagesView.hpp"
#include "views/MessageDetailView.hpp"
#include "views/MessageComposeView.hpp"
#include "core/Event.hpp"
#include "core/UiMemory.hpp"
#include "core/state.h"
#include "core/graphics.h"
#include "interfaces/keyboard.h"
#include "rtx/rtx.h"
#include "hwconfig.h"

#include <cstdio>

namespace ortxui
{

/** FNV-1a offset basis. */
static constexpr uint32_t FNV_BASIS = 2166136261u;

/** FNV-1a prime. */
static constexpr uint32_t FNV_PRIME = 16777619u;

void MessagesView::build()
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;
    const bool regular = (sizeClass() == SizeClass::Regular);

    root_.setArea({ 0, 0, (uint16_t)W, (uint16_t)H });
    root_.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(messages));

    list_.setContentPad(1);
    list_.setFlag(FLAG_FOCUSABLE, true);
    list_.setGrow(1);

    empty_.setFont(regular ? FONT_SIZE_10PT : FONT_SIZE_8PT);
    empty_.setAlign(TEXT_ALIGN_CENTER);
    empty_.setColor(Sem::OnSurfaceMuted);
    empty_.setText(STR(noMessages));
    empty_.setGrow(1);

    root_.addChild(&bar);
    root_.addChild(&list_);
    root_.addChild(&empty_);
    screen_.addChild(&root_);

    reload(state.channel.mode);
}

void MessagesView::rowAt(uint16_t index, ListItem &out) const
{
    out.checkbox = false;
    out.value = nullptr;

    const struct message *msg = messages_get(index);
    if (msg == nullptr) {
        out.label = (index == count_) ? STR(newMessage) : UiText("");
        return;
    }

    const char *other = (msg->direction == MSG_DIR_TX) ? msg->recipient :
                                                         msg->sender;
    const char *mark = "";
    if (msg->status == MSG_STATUS_FAILED)
        mark = SYMBOL_WARNING " ";
    else if (msg->unread)
        mark = SYMBOL_ENVELOPE " ";

    sniprintf(label_, sizeof(label_), "%s%s", mark, other);
    out.label = label_;
    out.value = msg->body;
}

uint32_t MessagesView::digest(uint8_t mode) const
{
    /* FNV-1a over what the rows show; the inbox holds a few dozen entries,
     * so walking it once per frame is cheap. */
    const size_t n = messages_count();
    uint32_t h = FNV_BASIS;
    h = (h ^ (uint32_t)n) * FNV_PRIME;
    h = (h ^ (messages_canCompose(mode) ? 1u : 0u)) * FNV_PRIME;
    for (size_t i = 0; i < n; i++) {
        const struct message *msg = messages_get(i);
        const uint32_t flags = ((uint32_t)msg->status << 1) | msg->unread;
        h = (h ^ msg->sequence) * FNV_PRIME;
        h = (h ^ flags) * FNV_PRIME;
    }
    return h;
}

uint16_t MessagesView::pinnedRow() const
{
    if (pinned_ == NEW_ROW)
        return canCompose_ ? count_ : 0;

    const size_t idx = messages_findBySequence(pinned_);
    return (idx < count_) ? static_cast<uint16_t>(idx) : 0;
}

void MessagesView::pin()
{
    const uint16_t sel = list_.selected();
    const struct message *msg = messages_get(sel);
    if (msg != nullptr)
        pinned_ = msg->sequence;
    else
        pinned_ = (canCompose_ && (sel == count_)) ? NEW_ROW : 0;
}

void MessagesView::reload(uint8_t mode)
{
    count_ = static_cast<uint16_t>(messages_count());
    canCompose_ = messages_canCompose(mode);
    digest_ = digest(mode);

    const bool empty = (rowCount() == 0);
    list_.setModel(this);
    list_.setSelected(pinnedRow(), false);
    list_.setFlag(FLAG_HIDDEN, empty);
    empty_.setFlag(FLAG_HIDDEN, !empty);

    root_.onLayout();
    if (!empty)
        screen_.focusFirst();
    screen_.markAllDirty();
}

void MessagesView::save(UiMemory &m) const
{
    m.inboxPinned = pinned_;
}

void MessagesView::restore(UiMemory &m)
{
    /* Back from the detail view this is the message it ended on. */
    pinned_ = m.inboxPinned;
}

void MessagesView::onShow()
{
    reload(state.channel.mode);
}

void MessagesView::syncFromState(const state_t &s)
{
    View::syncFromState(s);

    if (digest(s.channel.mode) != digest_)
        reload(s.channel.mode);
}

NavIntent MessagesView::onEvent(const Event &e)
{
    if (e.kind == EvKind::Key) {
        if ((e.keys & KEY_ESC) != 0u) {
            /* Unpin, so the next visit starts at the newest message. */
            pinned_ = 0;
            return NavIntent::pop();
        }

        if ((e.keys & KEY_ENTER) != 0u) {
            const uint16_t sel = list_.selected();
            const struct message *msg = messages_get(sel);
            if (msg != nullptr) {
                pinned_ = msg->sequence;
                MessageDetailView::open(msg->sequence);
                return NavIntent::push(ViewId::MessageDetail);
            }

            if (canCompose_ && (sel == count_)) {
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

    screen_.dispatch(e);
    pin();
    return NavIntent::none();
}

} // namespace ortxui
