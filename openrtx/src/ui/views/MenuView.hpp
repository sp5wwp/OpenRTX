/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_MENUVIEW_HPP
#define ORTX_UI_MENUVIEW_HPP

#include <cstdint>
#include "core/View.hpp"
#include "widgets/Widgets.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/List.hpp"

namespace ortxui
{

/**
 * A generic vertical menu screen: a title bar over a selectable List.
 *
 * One class backs every menu level (main menu, settings, ...): build() takes a
 * title and a borrowed item table, and each row may be wired to a sub-View that
 * ENTER pushes onto the Navigator. Rows with no target are leaves and do
 * nothing on ENTER for now (their detail views are ported in later slices).
 * ESC pops back. Widgets are direct members in static storage, no heap.
 */
class MenuView : public View
{
public:
    static constexpr uint16_t kMaxRows = 24;

    /**
     * Build the menu.
     *
     * @param title: menu title.
     * @param items: row labels, copied.
     * @param count: number of rows, at most kMaxRows.
     */
    void build(const UiText &title, const UiText *items, uint16_t count);

    /** Wire a row so ENTER on it pushes `target` (nullptr leaves it a leaf). */
    void setRowTarget(uint16_t row, View *target);

    NavIntent onEvent(const Event &e) override;
    void announce() override;
    Screen &screen() override
    {
        return screen_;
    }

private:
    Screen screen_;
    TopBar topBar_;
    List list_;

    UiText title_;
    ListItem items_[kMaxRows] = {};
    View *rowTargets_[kMaxRows] = {};
};

} // namespace ortxui

#endif /* ORTX_UI_MENUVIEW_HPP */
