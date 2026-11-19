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
 * The Settings > M17 screen (value-row style), mirroring the classic M17 menu:
 *  - Callsign: edited in place with the cursor character in brackets
 *    ("W1[A]W"); ENTER stores it, ESC restores it.
 *  - Meta Txt: shown read-only for now (long free text wants a richer editor).
 *  - CAN: 0-15, cycled with UP/DOWN.
 *  - CAN RX Check: On/Off, toggled with UP/DOWN.
 *
 * All fields live in state.settings (persisted at shutdown); none need an rtx
 * resync. The base View handles ESC/scroll when not editing.
 */
class M17View : public SettingsListView
{
public:
    /** Constructor. */
    M17View() = default;

    /** Destructor. */
    ~M17View() = default;

    void build() override;
    void syncFromState(const state_t &s) override;

private:
    /* SettingsListView hooks: Callsign is Custom (cursor-cycle editor), Meta is
     * an Action (modal push), CAN is a Stepper, CAN RX is a Checkbox. */
    RowKind rowKind(uint8_t row) const override;
    void formatValue(uint8_t row, char *out, size_t cap) override;
    void setValueText(uint8_t row, const char *s) override;
    void onAdjust(uint8_t row, int dir) override;
    void onToggle(uint8_t row, bool on) override;
    NavIntent onActivate(uint8_t row) override;
    void onBeginEdit(uint8_t row) override;
    bool onEditEvent(const Event &e) override;
    void refreshCustom(uint8_t row) override;

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

    std::array<ListItem, RowCount> items_ = {};
    char bufs_[RowCount][24] = {};

    char callBuf_[sizeof(settings_t::callsign)] = {}; ///< Callsign being edited
    TextInput callsign_; ///< Field editing callBuf_

    /* Change-gate mirrors. */
    char lastCall_[10] = { '\1' }; //< force first render
    char lastMeta_[53] = { '\1' }; //< force first render
    uint8_t lastCan_ = 0xFFu;
    uint8_t lastCanRx_ = 0xFFu;
};

} // namespace ortxui

#endif /* ORTX_UI_M17VIEW_HPP */
