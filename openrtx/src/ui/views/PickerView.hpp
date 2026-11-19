/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_PICKERVIEW_HPP
#define ORTX_UI_PICKERVIEW_HPP

#include <cstdint>
#include "base/View.hpp"
#include "views/PickerGrid.hpp"

namespace ortxui
{

/**
 * A full-screen character picker: a scrolling grid of code points (accented
 * Latin + symbols not on the keyboard) drawn from the custom icon/fallback
 * font. Pushed on top of the text editor; ENTER selects and pops, ESC cancels.
 * The selection and the choice go to the editor's request in UiMemory, which
 * the editor reads when it is rebuilt.
 * Navigated by arrows/knob (LEFT/RIGHT step, UP/DOWN by row).
 */
class PickerView : public View
{
public:
    /** Constructor. */
    PickerView() = default;

    /** Destructor. */
    ~PickerView() = default;

    /** Lay out the grid at the kept selection. */
    void build() override;

    /** Start a pick at the first cell. Call before pushing the picker. */
    static void open();

    /** Keep the selection while destroyed. */
    void save() const override;

    /** Speak the picker's title. */
    void announce() override;

    /**
     * Move the selection, or close the picker with or without a choice.
     *
     * @param e: event.
     * @return the navigation intent.
     */
    NavIntent onEvent(const Event &e) override;

    /** Screen the picker paints. */
    Screen &screen() override
    {
        return screen_;
    }

private:
    Screen screen_;   ///< Screen of the picker
    PickerGrid grid_; ///< Grid of characters
};

} // namespace ortxui

#endif /* ORTX_UI_PICKERVIEW_HPP */
