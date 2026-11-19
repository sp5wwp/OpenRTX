/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_CHECKLISTVIEW_HPP
#define ORTX_UI_CHECKLISTVIEW_HPP

#include <array>
#include <cstdint>
#include "core/SettingsListView.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The Settings > Accessibility screen: a list of checkboxes, each toggled by
 * ENTER into its state.settings flag.
 */
class ChecklistView : public SettingsListView
{
public:
    /** Constructor. */
    ChecklistView() = default;

    /** Destructor. */
    ~ChecklistView() = default;

    /** Build the checkboxes from the live settings. */
    void build() override;

private:
    /** Every row is a checkbox. */
    RowKind rowKind(uint8_t row) const override
    {
        (void)row;
        return RowKind::Checkbox;
    }

    /** Write a toggled row into its settings flag. */
    void onToggle(uint8_t row, bool on) override;

    /** Rows, top to bottom. */
    enum Row : uint8_t {
        RowPhonetic, ///< Spell callsigns and text phonetically
        RowLatch,    ///< Keep the macro menu open after MONI
        RowCount,
    };

    std::array<ListItem, RowCount> items_ = {}; ///< Rows
};

} // namespace ortxui

#endif /* ORTX_UI_CHECKLISTVIEW_HPP */
