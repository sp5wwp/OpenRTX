/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_CHECKLISTVIEW_HPP
#define ORTX_UI_CHECKLISTVIEW_HPP

#include <cstdint>
#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/List.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * A multi-toggle settings screen in the "checklist" style (mockup 4): a
 * selectable list of labelled checkboxes. ENTER toggles the selected row's
 * tick and writes the matching state.settings flag (persisted to flash at
 * shutdown, as in the classic UI). The boxes are seeded from settings on first
 * sync. The base View handles ESC/scroll.
 */
class ChecklistView : public View
{
public:
    void build();
    void syncFromState(const state_t &s) override;
    NavIntent onEvent(const Event &e) override;

    Screen &screen() override
    {
        return screen_;
    }

private:
    /** Persist the toggled row into the matching state.settings flag. */
    void applyToggle(uint16_t row, bool on);

    enum Row : uint8_t {
        RowPhonetic,
        RowLatch,
        RowCount,
    };

    Screen screen_;
    Flex root_;
    TopBar topBar_;
    List list_;

    ListItem items_[RowCount] = {};
    bool seeded_ = false;
};

} // namespace ortxui

#endif /* ORTX_UI_CHECKLISTVIEW_HPP */
