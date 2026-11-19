/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_MESSAGEDETAILVIEW_HPP
#define ORTX_UI_MESSAGEDETAILVIEW_HPP

#include <cstdint>
#include <cstddef>
#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Widgets.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/TextBlock.hpp"
#include "core/messages.h"

namespace ortxui
{

/**
 * One inbox message: a "sender > recipient" header, the scrollable body and a
 * footer with the Reply hint and the "n/m" position. UP/DOWN and the knob
 * scroll the body a line at a time and, at either end, step to the adjacent
 * message. ENTER replies to a received message of the current mode; ESC goes
 * back. The message is pinned by sequence number, so arrivals and evictions
 * cannot swap it for another; if it disappears the next key goes back.
 */
class MessageDetailView : public View
{
public:
    /** Build the widget tree. */
    void build() override;

    /**
     * Pin a message, shown from its top, and mark it read. Call before
     * pushing the view.
     *
     * @param sequence: sequence number of the message.
     */
    static void open(uint32_t sequence);

    /** Keep the pinned message and the body scroll while destroyed. */
    void save(UiMemory &m) const override;

    /** Take back the pinned message and the body scroll. */
    void restore(UiMemory &m) override;

    /** Read the message out: the other party, then the body. */
    void announce() override;

    /** Re-resolve the pinned message and refresh what changed. */
    void syncFromState(const state_t &s) override;

    /** Handle scrolling, stepping, reply and ESC. */
    NavIntent onEvent(const Event &e) override;

    /** The view's widget-tree root. */
    Screen &screen() override
    {
        return screen_;
    }

private:
    /**
     * Look up the pinned message and re-point the body at it, since entry
     * pointers do not survive a messages_task() call.
     *
     * @param idx: set to the message index when found.
     * @return the message, or nullptr if it is gone.
     */
    const struct message *current(size_t &idx);

    /** Pin entry `idx`, mark it read and read it out; `tail` shows its end. */
    void show(size_t idx, bool tail);

    /** Scroll the body by `dir`, or step to the adjacent message at an end. */
    void scrollOrStep(size_t idx, int dir);

    /** Fill the header, footer and body for `msg` at `idx`. */
    void refresh(const struct message *msg, size_t idx, bool reply);

    /** Whether `msg` can be answered while operating in `mode`. */
    static bool canReply(const struct message *msg, uint8_t mode);

    /** Widget-tree root. */
    Screen screen_;
    /** Column: top bar, header, body, footer. */
    Flex root_;
    /** "sender > recipient" line. */
    Label header_;
    /** Scrollable message body. */
    TextBlock body_;
    /** Footer row: Reply hint left, position right. */
    Flex footer_;
    /** Reply hint, blank when the message cannot be answered. */
    Label reply_;
    /** "n/m" position in the inbox. */
    Label position_;
    /** Sequence number of the pinned message. */
    uint32_t seq_ = 0;
    /** Sequence number the widgets currently show; 0 when none. */
    uint32_t shownSeq_ = 0;
    /** Index the position label currently shows. */
    size_t shownIdx_ = SIZE_MAX;
    /** Inbox size the position label currently shows. */
    size_t shownCount_ = 0;
    /** Whether the Reply hint is currently shown. */
    bool shownReply_ = false;
    /** Header text. */
    char headerBuf_[2 * MSG_ADDR_MAX_LEN + 4] = {};
    /** Position text. */
    char positionBuf_[12] = {};
};

} // namespace ortxui

#endif /* ORTX_UI_MESSAGEDETAILVIEW_HPP */
