/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_M17VIEW_HPP
#define ORTX_UI_M17VIEW_HPP

#include <array>
#include <cstdint>
#include <cstddef>
#include "core/SettingsListView.hpp"
#include "widgets/TextInput.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The Settings > M17 screen:
 *  - Callsign: edited in place with the cursor character in brackets
 *    ("W1[A]W"); ENTER stores it, ESC restores it.
 *  - Meta Txt: ENTER opens the text editor.
 *  - CAN: 0-15, stepped in place.
 *  - CAN RX Check: a checkbox, toggled by ENTER.
 *
 * Callsign, CAN and CAN RX are copied to the radio on an rtx sync, so a change
 * requests one.
 */
class M17View : public SettingsListView
{
public:
    /** Constructor. */
    M17View() = default;

    /** Destructor. */
    ~M17View() = default;

    /** Build the rows from the live settings. */
    void build() override;

private:
    /* SettingsListView hooks: Callsign is Custom (cursor-cycle editor), Meta is
     * an Action (modal push), CAN is a Stepper, CAN RX is a Checkbox. */
    RowKind rowKind(uint8_t row) const override;
    void formatValue(uint8_t row, char *out, size_t cap) override;
    void onAdjust(uint8_t row, int8_t dir) override;
    void onToggle(uint8_t row, bool on) override;
    NavIntent onActivate(uint8_t row) override;
    void onBeginEdit(uint8_t row) override;
    bool onEditEvent(const Event &e) override;
    void refreshCustom(uint8_t row, bool speak) override;

    /** Keep the callsign being edited. */
    void saveCustom(ListEdit &e) const override;

    /** Reopen the callsign edit kept by saveCustom(). */
    void restoreCustom(const ListEdit &e) override;

    /** Store the edited callsign and leave the edit. */
    void callsignConfirm();

    /** Speak the character under the cursor. */
    void announceCursorChar();

    enum Row : uint8_t {
        RowCallsign,
        RowMeta,
        RowCan,
        RowCanRx,
        RowCount,
    };

    std::array<ListItem, RowCount> items_ = {};       ///< Rows

    char callBuf_[sizeof(settings_t::callsign)] = {}; ///< Callsign being edited
    TextInput callsign_; ///< Field editing callBuf_
};

} // namespace ortxui

#endif /* ORTX_UI_M17VIEW_HPP */
