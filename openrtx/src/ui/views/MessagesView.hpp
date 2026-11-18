/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_MESSAGESVIEW_HPP
#define ORTX_UI_MESSAGESVIEW_HPP

#include <cstdint>
#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Widgets.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/List.hpp"
#include "core/messages.h"

namespace ortxui
{

class MessageDetailView;
class MessageComposeView;

/**
 * The main-menu message inbox, newest first. Each row shows the other party
 * (marked when unread or failed to send) and a body preview; while a protocol
 * source serves the current mode the list ends with a "New Message" row. ENTER
 * opens a message or the compose form, ESC goes back. The selection is pinned
 * by sequence number, so a message arriving or being evicted in the background
 * does not move it to another entry. With nothing to show or compose, a
 * centred "No messages" line replaces the list.
 */
class MessagesView : public View, public ListModel
{
public:
    /** Build the widget tree. */
    void build();

    /** Wire the detail view and the compose form this list opens. */
    void setViews(MessageDetailView *detail, MessageComposeView *compose)
    {
        detail_ = detail;
        compose_ = compose;
    }

    /** Reload the rows, keeping the selection on the pinned entry. */
    void onShow() override;

    /** Reload the rows when the inbox changed since the last frame. */
    void syncFromState(const state_t &s) override;

    /** Handle ENTER, ESC and list navigation. */
    NavIntent onEvent(const Event &e) override;

    /** The view's widget-tree root. */
    Screen &screen() override
    {
        return screen_;
    }

    /** Number of rows: the messages plus the "New Message" row, if any. */
    uint16_t rowCount() const override
    {
        return static_cast<uint16_t>(count_ + (canCompose_ ? 1 : 0));
    }

    /** Fill row `index` from the inbox. */
    void rowAt(uint16_t index, ListItem &out) const override;

private:
    /** Re-read the inbox for `mode` and restore the pinned selection. */
    void reload(uint8_t mode);

    /** Pin the selected row's entry so it survives index shifts. */
    void pin();

    /** Row index of the pinned entry, or the newest when it is gone. */
    uint16_t pinnedRow() const;

    /** Digest of the rows' content, to notice inbox changes cheaply. */
    uint32_t digest(uint8_t mode) const;

    /** Pin value of the "New Message" row. */
    static constexpr uint32_t NEW_ROW = UINT32_MAX;

    /** Widget-tree root. */
    Screen screen_;
    /** Column: top bar over the list or the empty-state line. */
    Flex root_;
    /** Title bar. */
    TopBar topBar_;
    /** Message rows. */
    List list_;
    /** "No messages" line. */
    Label empty_;
    /** Detail view opened by ENTER on a message. */
    MessageDetailView *detail_ = nullptr;
    /** Compose form opened by ENTER on "New Message". */
    MessageComposeView *compose_ = nullptr;
    /** Sequence of the selected entry, NEW_ROW, or 0 for the newest. */
    uint32_t pinned_ = 0;
    /** digest() at the last reload. */
    uint32_t digest_ = 0;
    /** Number of messages at the last reload. */
    uint16_t count_ = 0;
    /** Whether the "New Message" row is shown. */
    bool canCompose_ = false;
    /** Whether the detail view was opened from here and not returned yet. */
    bool inDetail_ = false;
    /** Row label scratch: status mark and callsign. */
    mutable char label_[MSG_ADDR_MAX_LEN + 8] = {};
};

} // namespace ortxui

#endif /* ORTX_UI_MESSAGESVIEW_HPP */
