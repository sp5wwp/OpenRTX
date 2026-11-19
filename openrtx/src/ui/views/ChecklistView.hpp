/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_CHECKLISTVIEW_HPP
#define ORTX_UI_CHECKLISTVIEW_HPP

#include <array>
#include <cstdint>
#include <cstddef>
#include "views/SettingsListView.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The Settings > Accessibility screen: the macro latch and phonetic spelling
 * checkboxes, toggled by ENTER, and the voice prompt level, stepped in place
 * from Off through Beep to levels 1-3.
 */
class ChecklistView : public SettingsListView
{
public:
    /** Constructor. */
    ChecklistView() = default;

    /** Destructor. */
    ~ChecklistView() = default;

    /** Build the rows from the live settings. */
    void build() override;

private:
    /** Voice is a Stepper, the other rows are checkboxes. */
    RowKind rowKind(uint8_t row) const override;

    /** Render the voice prompt level. */
    void formatValue(uint8_t row, char *out, size_t cap) override;

    /** Step the voice prompt level. */
    void onAdjust(uint8_t row, int8_t dir) override;

    /** Write a toggled row into its settings flag. */
    void onToggle(uint8_t row, bool on) override;

    /** Speak the voice prompt level at any level, the rest as the base. */
    void announceValue(uint8_t row, const UiText &value,
                       vpQueueFlags flags) override;

    /** Rows, top to bottom. */
    enum Row : uint8_t {
        RowLatch,    ///< Keep the macro menu open after MONI
        RowVoice,    ///< Voice prompt level
        RowPhonetic, ///< Spell callsigns and text phonetically
        RowCount,    ///< Number of rows
    };

    std::array<ListItem, RowCount> items = {}; ///< Rows
};

} // namespace ortxui

#endif /* ORTX_UI_CHECKLISTVIEW_HPP */
