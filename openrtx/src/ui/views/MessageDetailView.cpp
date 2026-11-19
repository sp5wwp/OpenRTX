/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/MessageDetailView.hpp"
#include "views/MessageComposeView.hpp"
#include "base/Event.hpp"
#include "views/UiMemory.hpp"
#include "core/state.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"
#include "interfaces/keyboard.h"
#include "hwconfig.h"

#include <cstdio>

namespace ortxui
{

/** Whether the screen is the regular size class. */
static constexpr bool REGULAR = (sizeClass() == SizeClass::Regular);

/** Height of the header line. */
static constexpr int16_t HEADER_H = REGULAR ? 14 : 10;

/** Margin around the body. */
static constexpr uint8_t BODY_MARGIN = REGULAR ? 3 : 2;

/** Height of the footer. */
static constexpr int16_t FOOTER_H = REGULAR ? 12 : 9;

/** Horizontal padding of the footer. */
static constexpr int16_t FOOTER_PAD_X = 4;

void MessageDetailView::build()
{
    const fontSize_t text = REGULAR ? FONT_SIZE_8PT : FONT_SIZE_6PT;
    const fontSize_t small = REGULAR ? FONT_SIZE_6PT : FONT_SIZE_5PT;

    root.setArea({ 0, 0, CONFIG_SCREEN_WIDTH, CONFIG_SCREEN_HEIGHT });
    root.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(message));

    header.setFont(text);
    header.setColor(Sem::Accent);
    header.setAlign(TEXT_ALIGN_CENTER);
    header.setOverflow(Label::Overflow::Ellipsize);
    header.setText(headerBuf);
    header.setBasis(HEADER_H);

    body.setFont(text);
    body.setGrow(1);
    body.setMargin(BODY_MARGIN);

    replyLabel.setFont(small);
    replyLabel.setColor(Sem::Accent);
    replyLabel.setGrow(1);

    position.setFont(small);
    position.setColor(Sem::OnSurfaceMuted);
    position.setAlign(TEXT_ALIGN_RIGHT);
    position.setText(positionBuf);
    position.setGrow(1);

    footer.setAxis(Axis::Row);
    footer.setBackground(Sem::Surface);
    footer.setPadding(FOOTER_PAD_X, 0);
    footer.setBasis(FOOTER_H);
    footer.addChild(&replyLabel);
    footer.addChild(&position);

    root.addChild(&bar);
    root.addChild(&header);
    root.addChild(&body);
    root.addChild(&footer);
    tree.addChild(&root);
    root.onLayout();
}

void MessageDetailView::open(uint32_t sequence)
{
    uiMemory.messageSeq = sequence;
    uiMemory.messageScroll = 0;
    messages_markRead(messages_findBySequence(sequence), true);
}

void MessageDetailView::save() const
{
    uiMemory.messageSeq = seq;
    uiMemory.messageScroll = body.scrollOffset();
    /* The inbox follows the message this view ended on. */
    uiMemory.inboxPinned = seq;
}

void MessageDetailView::restore()
{
    seq = uiMemory.messageSeq;
    body.setScrollOffset(uiMemory.messageScroll);
}

bool MessageDetailView::canReply(const struct message *msg, uint8_t mode)
{
    return (msg->mode == mode) && (msg->direction == MSG_DIR_RX)
        && messages_canCompose(msg->mode);
}

const struct message *MessageDetailView::current(size_t &idx)
{
    idx = messages_findBySequence(seq);
    const struct message *msg = messages_get(idx);
    body.setText((msg != nullptr) ? msg->body : "");
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
    seq = messages_get(idx)->sequence;
    if (tail)
        body.scrollToEnd();
    else
        body.scrollToTop();
    messages_markRead(idx, true);
    announce();
}

void MessageDetailView::scrollOrStep(size_t idx, int dir)
{
    if (body.scroll(dir))
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
    sniprintf(headerBuf, sizeof(headerBuf), "%s > %s", msg->sender, rcpt);
    sniprintf(positionBuf, sizeof(positionBuf), "%u/%u",
              static_cast<unsigned>(idx + 1),
              static_cast<unsigned>(messages_count()));
    replyLabel.setText(reply ? STR(reply) : UiText(""));
    tree.markAllDirty();
}

void MessageDetailView::syncFromState(const state_t &s)
{
    View::syncFromState(s);

    size_t idx;
    const struct message *msg = current(idx);
    if (msg == nullptr) {
        /* Evicted or the inbox closed: keep a placeholder until the next
         * key goes back to the list. */
        if (shownSeq != 0) {
            sniprintf(headerBuf, sizeof(headerBuf), "%s",
                      STR(noMessages).str());
            positionBuf[0] = '\0';
            replyLabel.setText("");
            shownSeq = 0;
            tree.markAllDirty();
        }
        return;
    }

    const size_t count = messages_count();
    const bool reply = canReply(msg, s.channel.mode);
    if ((seq == shownSeq) && (idx == shownIdx) && (count == shownCount)
        && (reply == shownReply))
        return;

    refresh(msg, idx, reply);
    shownSeq = seq;
    shownIdx = idx;
    shownCount = count;
    shownReply = reply;
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
    /* A held arrow keeps scrolling; ESC and ENTER act once. */
    const bool repeat = (e.kind == EvKind::KeyRepeat);
    if ((e.kind != EvKind::Key) && !repeat)
        return NavIntent::none();

    const uint32_t k = e.keys;
    if (!repeat && ((k & KEY_ESC) != 0u))
        return NavIntent::pop();

    if (!repeat && ((k & KEY_ENTER) != 0u)) {
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
