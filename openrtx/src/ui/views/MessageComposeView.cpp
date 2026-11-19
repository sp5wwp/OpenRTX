/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/MessageComposeView.hpp"
#include "views/TextInputView.hpp"
#include "core/Event.hpp"
#include "core/UiMemory.hpp"
#include "core/state.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"
#include "interfaces/keyboard.h"
#include "style/Charsets.hpp"
#include "hwconfig.h"

#include <cstdlib>
#include <cstring>

namespace ortxui
{

void MessageComposeView::build()
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;
    const int16_t topH = (sizeClass() == SizeClass::Regular) ? 16 : 12;

    TopBar &bar = useTopBar(uiMemory.draftReply ? STR(reply) : STR(newMessage));
    bar.setArea({ 0, 0, (uint16_t)W, (uint16_t)topH });
    bar.onLayout();

    items_[ROW_TO].label = STR(to);
    items_[ROW_BODY].label = STR(message);
    items_[ROW_SEND].label = STR(send);
    refresh();

    list_.setArea({ 0, topH, (uint16_t)W, (uint16_t)(H - topH) });
    list_.setContentPad(1);
    list_.setItems(items_.data(), ROW_COUNT);
    list_.setFlag(FLAG_FOCUSABLE, true);

    screen_.addChild(&bar);
    screen_.addChild(&list_);
    screen_.focusFirst();
    screen_.markAllDirty();
}

bool MessageComposeView::open(const char *recipient, bool reply)
{
    /* Pay for the draft only while composing. */
    if (!uiMemory.openDraft(recipient, reply))
        return false;

    uiMemory.lists[static_cast<uint8_t>(ViewId::MessageCompose)] = { ROW_BODY,
                                                                     0 };
    return true;
}

void MessageComposeView::refresh()
{
    /* An empty recipient is a broadcast. */
    const ComposeDraft *d = uiMemory.draft;
    const bool to = (d != nullptr) && (d->recipient[0] != '\0');
    items_[ROW_TO].value = to ? UiText(d->recipient) : STR(broadcast);
    items_[ROW_BODY].value = (d != nullptr) ? d->body : "";
    list_.invalidate();
}

void MessageComposeView::onShow()
{
    refresh();
}

void MessageComposeView::announce()
{
    if (state.settings.vpLevel < vpLow)
        return;

    const ListItem *it = list_.selectedItem();
    vp_flush();
    topBar().title().announce(vp_getVoiceLevelQueueFlags());
    it->label.announce(vpqDefault);
    it->value.announce(vpqDefault);
    vp_play();
}

bool MessageComposeView::send()
{
    const ComposeDraft *d = uiMemory.draft;
    const size_t len = strnlen(d->body, MSG_BODY_MAX_LEN);
    if (len == 0)
        return false;

    /* An empty To field goes on the air as the source's broadcast address,
     * never the display string. */
    const uint8_t mode = state.channel.mode;
    const char *rcpt = (d->recipient[0] != '\0') ? d->recipient :
                                                   messages_broadcastAddr(mode);
    if (rcpt == nullptr) {
        vp_announceError(
            static_cast<vpQueueFlags>(vpqInit | vpqPlayImmediately));
        return false;
    }

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    msg.body = d->body;
    msg.bodyLen = static_cast<uint16_t>(len);
    msg.mode = mode;
    strncpy(msg.sender, state.settings.callsign, sizeof(msg.sender) - 1);
    strncpy(msg.recipient, rcpt, sizeof(msg.recipient) - 1);

    /* The entry records its own SENDING/SENT/FAILED status, so the form
     * closes whatever the immediate result: staying open on a transient
     * -EBUSY would only store a duplicate entry on each retry. */
    messages_send(&msg);
    return true;
}

NavIntent MessageComposeView::close()
{
    uiMemory.closeDraft();
    refresh();
    return NavIntent::pop();
}

NavIntent MessageComposeView::onEvent(const Event &e)
{
    if (e.kind != EvKind::Key) {
        screen_.dispatch(e);
        return NavIntent::none();
    }

    if ((e.keys & KEY_ESC) != 0u)
        return close();

    if ((e.keys & KEY_ENTER) == 0u) {
        screen_.dispatch(e);
        return NavIntent::none();
    }

    ComposeDraft *d = uiMemory.draft;
    switch (list_.selected()) {
        case ROW_TO:
            if (TextInputView::open(STR(to), d->recipient, sizeof(d->recipient),
                                    CHARSET_CALLSIGN, MTAP_CALLSIGN,
                                    /*multiline=*/false, /*rtx=*/false))
                return NavIntent::push(ViewId::TextInput);
            return NavIntent::none();

        case ROW_BODY:
            if (TextInputView::open(STR(message), d->body, sizeof(d->body),
                                    CHARSET_TEXT, MTAP_TEXT,
                                    /*multiline=*/true, /*rtx=*/false))
                return NavIntent::push(ViewId::TextInput);
            return NavIntent::none();

        default:
            if (send())
                return close();
            return NavIntent::none();
    }
}

} // namespace ortxui
