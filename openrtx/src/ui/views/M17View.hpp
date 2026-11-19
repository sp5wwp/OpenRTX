/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_M17VIEW_HPP
#define ORTX_UI_M17VIEW_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <array>
#include <cstdint>
#include <cstddef>
#include "views/SettingsListView.hpp"
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
    /** Callsign is Custom, Meta an Action, CAN a Stepper, CAN RX a Checkbox. */
    RowKind rowKind(uint8_t row) const override;

    /** Render the CAN. */
    void formatValue(uint8_t row, char *out, size_t cap) override;

    /** Step the CAN. */
    void onAdjust(uint8_t row, int8_t dir) override;

    /** Apply the CAN RX check. */
    void onToggle(uint8_t row, bool on) override;

    /** Speak the CAN as a number, the other rows as the base does. */
    void announceValue(uint8_t row, const UiText &value,
                       vpQueueFlags flags) override;

    /** Open the meta text editor. */
    NavIntent onActivate(uint8_t row) override;

    /** Load the callsign into its editor. */
    void onBeginEdit(uint8_t row) override;

    /** Drive the callsign editor. */
    bool onEditEvent(const Event &e) override;

    /** Render the callsign, bracketed at the cursor while it is edited. */
    void refreshCustom(uint8_t row, bool speak) override;

    /** Keep the callsign being edited. */
    void saveCustom(EditBlob<CUSTOM_EDIT> &e) const override;

    /** Reopen the callsign edit kept by saveCustom(). */
    void restoreCustom(const EditBlob<CUSTOM_EDIT> &e) override;

    /** Bind the callsign editor to callBuf. */
    void bindCallsign();

    /** Store the edited callsign and leave the edit. */
    void callsignConfirm();

    /** Speak the character under the cursor. */
    void announceCursorChar();

    /** Rows, top to bottom. */
    enum Row : uint8_t {
        RowCallsign, ///< Source callsign
        RowMeta,     ///< Meta text sent with a transmission
        RowCan,      ///< Channel access number
        RowCanRx,    ///< Receive only the CAN
        RowCount,    ///< Number of rows
    };

    /** A callsign edit kept while the view is destroyed. */
    struct CallsignEdit {
        char text[sizeof(settings_t::callsign)]; ///< Callsign being edited
        uint16_t cursor;                         ///< Cursor position
    };

    std::array<ListItem, RowCount> items = {};       ///< Rows

    char callBuf[sizeof(settings_t::callsign)] = {}; ///< Callsign being edited
    TextInput callsign;                              ///< Field editing callBuf
};

} // namespace ortxui

#endif /* ORTX_UI_M17VIEW_HPP */
