/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_MENUVIEW_HPP
#define ORTX_UI_MENUVIEW_HPP

#include <cstdint>
#include "views/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/List.hpp"

namespace ortxui
{

/**
 * One row of a menu: its label and the screen ENTER opens.
 */
struct MenuRow {
    UiText label;  ///< Row label
    ViewId target; ///< Screen the row opens
};

/**
 * A generic vertical menu screen: a title bar over a selectable List.
 *
 * One class backs every menu level (main menu, settings): id() picks the
 * title and the static row table, and ENTER pushes the selected row's screen.
 * ESC pops back.
 */
class MenuView : public View, public ListModel
{
public:
    /** Constructor. */
    MenuView() = default;

    /** Destructor. */
    ~MenuView() = default;

    /** Build the widget tree of the menu id() names. */
    void build() override;

    /** Open the selected row's screen, or go back on ESC. */
    NavIntent onEvent(const Event &e) override;

    /** The view's widget-tree root. */
    Screen &screen() override
    {
        return screen_;
    }

    /** The menu list, whose position UiMemory keeps. */
    List *list() override
    {
        return &list_;
    }

    /** Number of rows. */
    uint16_t rowCount() const override
    {
        return count_;
    }

    /** Fill a row from the menu table. */
    void rowAt(uint16_t index, ListItem &out) const override;

private:
    Screen screen_;                 ///< Widget tree root
    Flex root_;                     ///< Top bar over the list
    List list_;                     ///< Menu rows

    const MenuRow *rows_ = nullptr; ///< Static row table
    uint8_t count_ = 0;             ///< Number of rows
};

} // namespace ortxui

#endif /* ORTX_UI_MENUVIEW_HPP */
