/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_RADIOVIEW_HPP
#define ORTX_UI_RADIOVIEW_HPP

#include <array>
#include <cstdint>
#include <cstddef>
#include "core/SettingsListView.hpp"
#include "widgets/TextInput.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The Settings > Radio screen, for the current channel:
 *  - Offset: the TX/RX split. ENTER opens a keypad entry (shown as <N kHz>)
 *    pre-filled with the current split; digits retype it, '*' backspaces,
 *    UP/DOWN or the knob nudge it by the tuning step, ENTER applies it as
 *    tx = rx +/- offset in the current direction, ESC cancels.
 *  - Direction: '+' or '-', flipped by UP/DOWN.
 *  - Step: the tuning step, cycled through freq_steps.
 *
 * Offset and Direction change tx_frequency, so they request an rtx sync; Step
 * only changes the VFO increment.
 */
class RadioView : public SettingsListView
{
public:
    /** Constructor. */
    RadioView() = default;

    /** Destructor. */
    ~RadioView() = default;

    /** Build the rows from the current channel. */
    void build() override;

private:
    /** Offset is a Custom row, Direction and Step are Steppers. */
    RowKind rowKind(uint8_t row) const override;

    /** Render Direction or Step. */
    void formatValue(uint8_t row, char *out, size_t cap) override;

    /** Flip Direction or cycle Step. */
    void onAdjust(uint8_t row, int8_t dir) override;

    /** Pre-fill the offset entry. */
    void onBeginEdit(uint8_t row) override;

    /** Drive the offset entry. */
    bool onEditEvent(const Event &e) override;

    /** Render the offset, or the entry while it is open. */
    void refreshCustom(uint8_t row, bool speak) override;

    /** Speak Step with its unit, the other rows as the base does. */
    void announceValue(uint8_t row, const UiText &value,
                       vpQueueFlags flags) override;

    /**
     * Speak an offset entry key before it acts: the digit typed, or the one
     * '*' removes.
     *
     * @param keys: keys pressed.
     */
    void announceKey(uint32_t keys);

    /** Keep the offset being entered. */
    void saveCustom(EditBlob<CUSTOM_EDIT> &e) const override;

    /** Reopen the offset entry kept by saveCustom(). */
    void restoreCustom(const EditBlob<CUSTOM_EDIT> &e) override;

    /** Bind the offset entry to its buffer, emptied. */
    void bindOffset();

    /**
     * Nudge the entered offset by the tuning step.
     *
     * @param dir: +1 up, -1 down.
     */
    void offsetAdjust(int8_t dir);

    /** Apply tx = rx +/- the entered offset, if the radio can tune it. */
    void offsetApply();

    /**
     * Get the entered offset.
     *
     * @return the filled slots read as a decimal, in kHz.
     */
    uint32_t offsetKhz() const;

    /** Rows, top to bottom. */
    enum Row : uint8_t {
        RowOffset,    ///< TX/RX split
        RowDirection, ///< Sign of the split
        RowStep,      ///< VFO tuning step
        RowCount,
    };

    static constexpr size_t OFFSET_SIZE = 8; ///< Offset digits and the NUL

    /** An offset entry kept while the view is destroyed. */
    struct OffsetEdit {
        char digits[OFFSET_SIZE]; ///< Offset digits
        uint8_t slot;             ///< Cursor slot
        bool negative;            ///< The split is negative
        bool pristine;            ///< The digits are still the pre-fill
    };

    std::array<ListItem, RowCount> items_ = {}; ///< Rows
    TextInput offsetInput_;                     ///< Offset keypad entry
    char offsetBuf_[OFFSET_SIZE] = { 0 };       ///< Offset digits, in kHz
    bool offsetNeg_ = false;     ///< The split is negative (tx < rx)
    bool offsetPristine_ = true; ///< Pre-filled: the first digit retypes it
};

} // namespace ortxui

#endif /* ORTX_UI_RADIOVIEW_HPP */
