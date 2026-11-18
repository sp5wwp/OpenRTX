/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/MessageComposeView.hpp"
#include "views/TextInputView.hpp"
#include "core/Event.hpp"
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

    topBar_.init(STR(newMessage));
    topBar_.setArea({ 0, 0, (uint16_t)W, (uint16_t)topH });
    topBar_.onLayout();
    setTopBar(&topBar_);

    items_[ROW_TO].label = STR(to);
    items_[ROW_BODY].label = STR(message);
    items_[ROW_SEND].label = STR(send);
    refresh();

    list_.setArea({ 0, topH, (uint16_t)W, (uint16_t)(H - topH) });
    list_.setContentPad(1);
    list_.setItems(items_, ROW_COUNT);
    list_.setFlag(FLAG_FOCUSABLE, true);

    screen_.addChild(&topBar_);
    screen_.addChild(&list_);
    screen_.focusFirst();
    screen_.markAllDirty();
}

bool MessageComposeView::open(const char *recipient, bool reply)
{
    /* Reuse a draft left allocated if the form was not closed through
     * close(); otherwise pay for the body only while composing. */
    if (body_ == nullptr)
        body_ = static_cast<char *>(malloc(MSG_BODY_MAX_LEN));
    if (body_ == nullptr)
        return false;

    body_[0] = '\0';
    memset(recipient_, 0, sizeof(recipient_));
    if (recipient != nullptr)
        strncpy(recipient_, recipient, sizeof(recipient_) - 1);

    topBar_.setTitle(reply ? STR(reply) : STR(newMessage));
    refresh();
    list_.setSelected(ROW_BODY, false);
    return true;
}

void MessageComposeView::refresh()
{
    /* An empty recipient is a broadcast. */
    items_[ROW_TO].value = (recipient_[0] != '\0') ? UiText(recipient_) :
                                                     STR(broadcast);
    items_[ROW_BODY].value = (body_ != nullptr) ? body_ : "";
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
    topBar_.title().announce(vp_getVoiceLevelQueueFlags());
    it->label.announce(vpqDefault);
    it->value.announce(vpqDefault);
    vp_play();
}

bool MessageComposeView::send()
{
    const size_t len = strnlen(body_, MSG_BODY_MAX_LEN);
    if (len == 0)
        return false;

    /* An empty To field goes on the air as the source's broadcast address,
     * never the display string. */
    const uint8_t mode = state.channel.mode;
    const char *rcpt = (recipient_[0] != '\0') ? recipient_ :
                                                 messages_broadcastAddr(mode);
    if (rcpt == nullptr) {
        vp_announceError(
            static_cast<vpQueueFlags>(vpqInit | vpqPlayImmediately));
        return false;
    }

    struct message msg;
    memset(&msg, 0, sizeof(msg));
    msg.body = body_;
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
    free(body_);
    body_ = nullptr;
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

    switch (list_.selected()) {
        case ROW_TO:
            editor_->open(STR(to), recipient_, sizeof(recipient_),
                          CHARSET_CALLSIGN, MTAP_CALLSIGN,
                          /*multiline=*/false, /*rtx=*/false);
            return NavIntent::push(editor_);

        case ROW_BODY:
            editor_->open(STR(message), body_, MSG_BODY_MAX_LEN, CHARSET_TEXT,
                          MTAP_TEXT, /*multiline=*/true, /*rtx=*/false);
            return NavIntent::push(editor_);

        default:
            if (send())
                return close();
            return NavIntent::none();
    }
}

} // namespace ortxui
