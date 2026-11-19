/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_MESSAGESVIEW_HPP
#define ORTX_UI_MESSAGESVIEW_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>
#include "views/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Label.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/List.hpp"
#include "core/messages.h"

namespace ortxui
{

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
    /** Constructor. */
    MessagesView() = default;

    /** Destructor. */
    ~MessagesView() = default;

    /** Build the widget tree. */
    void build() override;

    /** Keep the pinned entry while destroyed. */
    void save() const override;

    /** Take back the pinned entry, which the detail view may have moved. */
    void restore() override;

    /** Load the rows, with the selection on the pinned entry. */
    void onShow() override;

    /** Reload the rows when the inbox changed since the last frame. */
    void syncFromState(const state_t &s) override;

    /** Handle ENTER, ESC and list navigation. */
    NavIntent onEvent(const Event &e) override;

    /** The view's widget-tree root. */
    Screen &screen() override
    {
        return tree;
    }

    /** Number of rows: the messages plus the "New Message" row, if any. */
    uint16_t rowCount() const override
    {
        return static_cast<uint16_t>(count + (canCompose ? 1 : 0));
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

    Screen tree;              ///< Widget-tree root
    Flex root;                ///< Top bar over the list or the empty line
    List listBox;             ///< Message rows
    Label emptyLabel;         ///< "No messages" line
    uint32_t pinned = 0;      ///< Selected sequence, NEW_ROW, 0 for newest
    uint32_t shownDigest = 0; ///< digest() at the last reload
    uint16_t count = 0;       ///< Number of messages at the last reload
    bool canCompose = false;  ///< The "New Message" row is shown
    mutable char label[MSG_ADDR_MAX_LEN + 8] = {}; ///< Mark and callsign
};

} // namespace ortxui

#endif /* ORTX_UI_MESSAGESVIEW_HPP */
