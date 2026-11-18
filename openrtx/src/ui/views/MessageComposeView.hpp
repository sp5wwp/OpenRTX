/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_MESSAGECOMPOSEVIEW_HPP
#define ORTX_UI_MESSAGECOMPOSEVIEW_HPP

#include <cstdint>
#include "core/View.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/List.hpp"
#include "core/messages.h"

namespace ortxui
{

class TextInputView;

/**
 * The message compose form: a To row (empty means broadcast), a Message row
 * and a Send row. ENTER on To or Message opens the shared modal text editor;
 * ENTER on Send hands the draft to the inbox and closes the form. ESC discards
 * the draft. The body lives on the heap only while the form is open.
 */
class MessageComposeView : public View
{
public:
    /** Build the widget tree. */
    void build();

    /** Wire the shared modal text editor. */
    void setTextEditor(TextInputView *e)
    {
        editor_ = e;
    }

    /**
     * Start a draft, allocating its body. Call right before pushing the view.
     *
     * @param recipient: initial recipient, or nullptr for broadcast.
     * @param reply: whether the draft answers a received message.
     * @return false if the body could not be allocated (do not push).
     */
    bool open(const char *recipient, bool reply);

    /** Refresh the row values after the editor returns. */
    void onShow() override;

    /** Speak the title and the selected row. */
    void announce() override;

    /** Handle row activation, ESC and list navigation. */
    NavIntent onEvent(const Event &e) override;

    /** The view's widget-tree root. */
    Screen &screen() override
    {
        return screen_;
    }

private:
    /** Rows of the form, top to bottom. */
    enum Row : uint8_t {
        ROW_TO,
        ROW_BODY,
        ROW_SEND,
        ROW_COUNT,
    };

    /** Hand the draft to the inbox; returns true once it is handed over. */
    bool send();

    /** Release the draft body and leave the form. */
    NavIntent close();

    /** Point the row values at the current draft. */
    void refresh();

    /** Widget-tree root. */
    Screen screen_;
    /** Title bar: "New Message" or "Reply". */
    TopBar topBar_;
    /** The To / Message / Send rows. */
    List list_;
    /** Row items, values borrowed from the draft. */
    ListItem items_[ROW_COUNT] = {};
    /** Shared modal text editor. */
    TextInputView *editor_ = nullptr;
    /** Draft body, MSG_BODY_MAX_LEN bytes while the form is open. */
    char *body_ = nullptr;
    /** Draft recipient; empty means broadcast. */
    char recipient_[MSG_ADDR_MAX_LEN] = {};
};

} // namespace ortxui

#endif /* ORTX_UI_MESSAGECOMPOSEVIEW_HPP */
