/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_MESSAGECOMPOSEVIEW_HPP
#define ORTX_UI_MESSAGECOMPOSEVIEW_HPP

#include <array>
#include <cstdint>
#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/List.hpp"
#include "core/messages.h"

namespace ortxui
{

/**
 * The message compose form: a To row (empty means broadcast), a Message row
 * and a Send row. ENTER on To or Message opens the shared modal text editor;
 * ENTER on Send hands the draft to the inbox and closes the form. ESC discards
 * the draft. The draft lives in UiMemory, on the heap, only while the form is
 * open, so the editor can write it while this view is destroyed.
 */
class MessageComposeView : public View
{
public:
    /** Constructor. */
    MessageComposeView() = default;

    /** Destructor. */
    ~MessageComposeView() = default;

    /** Build the widget tree. */
    void build() override;

    /**
     * Start a draft, allocating it. Call right before pushing the view.
     *
     * @param recipient: initial recipient, or nullptr for broadcast.
     * @param reply: whether the draft answers a received message.
     * @return false if the draft could not be allocated (do not push).
     */
    static bool open(const char *recipient, bool reply);

    /** Speak the title and the selected row. */
    void announce() override;

    /** Handle row activation, ESC and list navigation. */
    NavIntent onEvent(const Event &e) override;

    /** The view's widget-tree root. */
    Screen &screen() override
    {
        return screen_;
    }

    /** The form rows, whose position UiMemory keeps. */
    List *list() override
    {
        return &list_;
    }

private:
    /** Hand the draft to the inbox; returns true once it is handed over. */
    bool send();

    /** Release the draft body and leave the form. */
    NavIntent close();

    /** Point the row values at the current draft. */
    void refresh();

    /** Rows of the form, top to bottom. */
    enum Row : uint8_t {
        ROW_TO,
        ROW_BODY,
        ROW_SEND,
        ROW_COUNT,
    };

    /** Widget-tree root. */
    Screen screen_;
    /** Column: top bar over the rows. */
    Flex root_;
    /** The To / Message / Send rows. */
    List list_;
    /** Row items, values borrowed from the draft. */
    std::array<ListItem, ROW_COUNT> items_ = {};
};

} // namespace ortxui

#endif /* ORTX_UI_MESSAGECOMPOSEVIEW_HPP */
