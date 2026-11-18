/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_RADIOVIEW_HPP
#define ORTX_UI_RADIOVIEW_HPP

#include <cstdint>
#include <cstddef>
#include "core/SettingsListView.hpp"
#include "widgets/TextInput.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The Settings > Radio screen (value-row style), mirroring the classic Radio
 * menu for the current channel:
 *  - Offset: the TX/RX split. ENTER opens a keypad entry (value shown as
 *    <N kHz>), pre-filled with the current offset so re-selecting preserves it;
 *    digit keys retype it, '*' backspaces, UP/DOWN or the knob nudge it by the
 *    tuning step (like Step), ENTER applies it as tx = rx +/- offset (keeping the
 *    current direction), ESC cancels.
 *  - Direction: '+' or '-'. ENTER opens edit; UP/DOWN flips the split sign.
 *  - Step: the tuning step; ENTER opens edit; UP/DOWN cycles freq_steps.
 *
 * Offset and Direction change tx_frequency, so they request an rtx resync;
 * Step only changes the tuning increment (state.step_index) and does not.
 */
class RadioView : public SettingsListView
{
public:
    void build();
    void syncFromState(const state_t &s) override;

private:
    enum Row : uint8_t {
        RowOffset,
        RowDirection,
        RowStep,
        RowCount,
    };

    /* SettingsListView hooks: Direction/Step are Steppers; Offset is a Custom
     * (TextInput keypad) row the view drives itself. */
    RowKind rowKind(uint8_t row) const override;
    void formatValue(uint8_t row, char *out, size_t cap) override;
    void setValueText(uint8_t row, const char *s) override;
    void onAdjust(uint8_t row, int dir) override;
    void onBeginEdit(uint8_t row) override;
    bool onEditEvent(const Event &e) override;
    void refreshCustom(uint8_t row) override;

    void
    offsetAdjust(int dir); //< nudge the offset by the tuning step (knob/UP)
    void offsetApply();    //< commit tx = rx +/- the entered offset
    uint32_t offsetKhz() const; //< the entered offset (filled slots as decimal)

    ListItem items_[RowCount] = {};
    char bufs_[RowCount][20] = {};

    /* Offset keypad entry on the shared TextInput slot engine (6-digit kHz).
     * Pre-filled from the current offset magnitude so re-selecting preserves it;
     * offsetNeg_ keeps the current +/- direction across an edit. */
    TextInput offsetInput_;
    char offsetBuf_[8] = { 0 };
    bool offsetNeg_ = false;     //< current split direction (tx < rx)
    bool offsetPristine_ = true; //< pre-filled/untouched -> first digit retypes

    /* Change-gate mirrors. */
    uint32_t lastTx_ = 0xFFFFFFFFu;
    uint32_t lastRx_ = 0xFFFFFFFFu;
    uint8_t lastStep_ = 0xFFu;
};

} // namespace ortxui

#endif /* ORTX_UI_RADIOVIEW_HPP */
