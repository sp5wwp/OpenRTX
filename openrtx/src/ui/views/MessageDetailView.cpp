/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/MessageDetailView.hpp"
#include "views/MessageComposeView.hpp"
#include "core/Event.hpp"
#include "core/UiMemory.hpp"
#include "core/state.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"
#include "interfaces/keyboard.h"
#include "hwconfig.h"

#include <cstdio>

namespace ortxui
{

void MessageDetailView::build()
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;
    const bool regular = (sizeClass() == SizeClass::Regular);
    const fontSize_t small = regular ? FONT_SIZE_6PT : FONT_SIZE_5PT;

    root_.setArea({ 0, 0, (uint16_t)W, (uint16_t)H });
    root_.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(message));

    header_.setFont(regular ? FONT_SIZE_8PT : FONT_SIZE_6PT);
    header_.setColor(Sem::Accent);
    header_.setAlign(TEXT_ALIGN_CENTER);
    header_.setOverflow(Label::Overflow::Ellipsize);
    header_.setText(headerBuf_);
    header_.setBasis(regular ? 14 : 10);

    body_.setFont(regular ? FONT_SIZE_8PT : FONT_SIZE_6PT);
    body_.setGrow(1);
    body_.setMargin(regular ? 3 : 2);

    reply_.setFont(small);
    reply_.setColor(Sem::Accent);
    reply_.setGrow(1);

    position_.setFont(small);
    position_.setColor(Sem::OnSurfaceMuted);
    position_.setAlign(TEXT_ALIGN_RIGHT);
    position_.setText(positionBuf_);
    position_.setGrow(1);

    footer_.setAxis(Axis::Row);
    footer_.setBackground(Sem::Surface);
    footer_.setPadding(4, 0);
    footer_.setBasis(regular ? 12 : 9);
    footer_.addChild(&reply_);
    footer_.addChild(&position_);

    root_.addChild(&bar);
    root_.addChild(&header_);
    root_.addChild(&body_);
    root_.addChild(&footer_);
    screen_.addChild(&root_);
    root_.onLayout();
    screen_.markAllDirty();
}

void MessageDetailView::open(uint32_t sequence)
{
    uiMemory.messageSeq = sequence;
    uiMemory.messageScroll = 0;
    messages_markRead(messages_findBySequence(sequence), true);
}

void MessageDetailView::save(UiMemory &m) const
{
    m.messageSeq = seq_;
    m.messageScroll = body_.scrollOffset();
    /* The inbox follows the message this view ended on. */
    m.inboxPinned = seq_;
}

void MessageDetailView::restore(UiMemory &m)
{
    seq_ = m.messageSeq;
    body_.setScrollOffset(m.messageScroll);
}

bool MessageDetailView::canReply(const struct message *msg, uint8_t mode)
{
    return (msg->mode == mode) && (msg->direction == MSG_DIR_RX)
        && messages_canCompose(msg->mode);
}

const struct message *MessageDetailView::current(size_t &idx)
{
    idx = messages_findBySequence(seq_);
    const struct message *msg = messages_get(idx);
    body_.setText((msg != nullptr) ? msg->body : "");
    return msg;
}

void MessageDetailView::announce()
{
    if (state.settings.vpLevel < vpLow)
        return;

    size_t idx;
    const struct message *msg = current(idx);
    if (msg == nullptr)
        return;

    const char *other = (msg->direction == MSG_DIR_TX) ? msg->recipient :
                                                         msg->sender;
    vp_flush();
    vp_queueString(other, vpAnnounceCommonSymbols);
    vp_queuePrompt(PROMPT_SILENCE);
    vp_queueString(msg->body, vpAnnounceCommonSymbols);
    vp_play();
}

void MessageDetailView::show(size_t idx, bool tail)
{
    seq_ = messages_get(idx)->sequence;
    if (tail)
        body_.scrollToEnd();
    else
        body_.scrollToTop();
    messages_markRead(idx, true);
    announce();
}

void MessageDetailView::scrollOrStep(size_t idx, int dir)
{
    if (body_.scroll(dir))
        return;

    /* Newest first: stepping up opens the newer message at its end. */
    if ((dir < 0) && (idx > 0))
        show(idx - 1, true);
    else if ((dir > 0) && (idx + 1 < messages_count()))
        show(idx + 1, false);
}

void MessageDetailView::refresh(const struct message *msg, size_t idx,
                                bool reply)
{
    const char *rcpt = (msg->recipient[0] != '\0') ? msg->recipient :
                                                     STR(broadcast).str();
    snprintf(headerBuf_, sizeof(headerBuf_), "%s > %s", msg->sender, rcpt);
    snprintf(positionBuf_, sizeof(positionBuf_), "%u/%u", (unsigned)(idx + 1),
             (unsigned)messages_count());
    reply_.setText(reply ? STR(reply) : UiText(""));
    screen_.markAllDirty();
}

void MessageDetailView::syncFromState(const state_t &s)
{
    View::syncFromState(s);

    size_t idx;
    const struct message *msg = current(idx);
    if (msg == nullptr) {
        /* Evicted or the inbox closed: keep a placeholder until the next
         * key goes back to the list. */
        if (shownSeq_ != 0) {
            snprintf(headerBuf_, sizeof(headerBuf_), "%s",
                     STR(noMessages).str());
            positionBuf_[0] = '\0';
            reply_.setText("");
            shownSeq_ = 0;
            screen_.markAllDirty();
        }
        return;
    }

    const size_t count = messages_count();
    const bool reply = canReply(msg, s.channel.mode);
    if ((seq_ == shownSeq_) && (idx == shownIdx_) && (count == shownCount_)
        && (reply == shownReply_))
        return;

    refresh(msg, idx, reply);
    shownSeq_ = seq_;
    shownIdx_ = idx;
    shownCount_ = count;
    shownReply_ = reply;
}

NavIntent MessageDetailView::onEvent(const Event &e)
{
    size_t idx;
    const struct message *msg = current(idx);
    if (msg == nullptr)
        return NavIntent::pop();

    if (e.kind == EvKind::Encoder) {
        scrollOrStep(idx, e.encoder);
        return NavIntent::none();
    }
    if (e.kind != EvKind::Key)
        return NavIntent::none();

    const uint32_t k = e.keys;
    if ((k & KEY_ESC) != 0u)
        return NavIntent::pop();

    if ((k & KEY_ENTER) != 0u) {
        if (canReply(msg, state.channel.mode)
            && MessageComposeView::open(msg->sender, true))
            return NavIntent::push(ViewId::MessageCompose);
        return NavIntent::none();
    }

    if ((k & KEY_UP) != 0u)
        scrollOrStep(idx, -1);
    else if ((k & KEY_DOWN) != 0u)
        scrollOrStep(idx, +1);
    return NavIntent::none();
}

} // namespace ortxui
