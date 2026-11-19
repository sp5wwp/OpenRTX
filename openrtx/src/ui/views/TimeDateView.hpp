/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_TIMEDATEVIEW_HPP
#define ORTX_UI_TIMEDATEVIEW_HPP

#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Label.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/TextInput.hpp"
#include "core/state.h"
#include "core/datetime.h"

namespace ortxui
{

/**
 * The Settings > Time & Date screen (CONFIG_RTC). Shows the local date and
 * time; ENTER opens a keypad entry of dd/mm/yy hh:mm, seconds at 00. Digits
 * fill the fields left to right and '*' backspaces; ENTER sets the clock to a
 * complete, valid entry, converted to UTC, and ESC cancels.
 */
class TimeDateView : public View
{
public:
    /** Constructor. */
    TimeDateView() = default;

    /** Destructor. */
    ~TimeDateView() = default;

    /** Build the clock display. */
    void build() override;

    /** Speak the title, date and time. */
    void announce() override;

    /** Keep an open entry while destroyed. */
    void save() const override;

    /** Reopen an entry kept by save(). */
    void restore() override;

    /** Follow the clock while not editing. */
    void syncFromState(const state_t &s) override;

    /** Edit, commit or cancel the entry, or go back on ESC. */
    NavIntent onEvent(const Event &e) override;

    /** The view's widget-tree root. */
    Screen &screen() override
    {
        return screen_;
    }

private:
    /**
     * Show a time as local date and time.
     *
     * @param s: state snapshot.
     */
    void showDisplay(const state_t &s);

    /**
     * Show a text in a label if it changed.
     *
     * @param label: label.
     * @param buf: the label's text buffer, TEXT_SIZE bytes.
     * @param text: text to show.
     */
    static void showText(Label &label, char *buf, const char *text);

    /** Open the entry, empty, and speak its format. */
    void beginEdit();

    /** Open the entry, empty. */
    void openEntry();

    /** Close the entry and show the clock. */
    void endEdit();

    /** Set the clock to the entry, if it is a valid date and time. */
    void commit();

    /** Show the entry, '_' for each digit not typed yet. */
    void refreshEdit();

    /** Speak the digit just typed, with the separator after it. */
    void announceDigit() const;

    /**
     * Get a two-digit field of the entry.
     *
     * @param first: slot of the field's first digit.
     * @return the field's value.
     */
    int8_t field(uint8_t first) const;

    static constexpr size_t TEXT_SIZE = 16;  ///< Size of a shown text
    static constexpr size_t ENTRY_SIZE = 11; ///< Entry digits and the NUL

    /** An entry kept while the view is destroyed. */
    struct DateEdit {
        char digits[ENTRY_SIZE]; ///< Entry digits
        uint16_t slot;           ///< Cursor slot
    };

    Screen screen_;                  ///< Widget tree root
    Flex root_;                      ///< Top bar over the body
    Flex body_;                      ///< Date over time over the hint
    Label dateLabel_;                ///< Date, or its entry
    Label timeLabel_;                ///< Time, or its entry
    Label hint_;                     ///< Key hint
    TextInput input_;                ///< Digit entry: dd mm yy hh mm
    char inputBuf_[ENTRY_SIZE] = {}; ///< Entry digits
    char dateBuf_[TEXT_SIZE] = {};   ///< Date text shown
    char timeBuf_[TEXT_SIZE] = {};   ///< Time text shown
    bool editing_ = false;           ///< The entry is open
};

} // namespace ortxui

#endif /* ORTX_UI_TIMEDATEVIEW_HPP */
