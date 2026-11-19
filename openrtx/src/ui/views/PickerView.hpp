/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_PICKERVIEW_HPP
#define ORTX_UI_PICKERVIEW_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>
#include "views/View.hpp"
#include "widgets/PickerGrid.hpp"

namespace ortxui
{

/**
 * A full-screen character picker over the text editor: a scrolling grid of the
 * accented letters and symbols the keypad does not type. LEFT/RIGHT and the
 * knob step, UP/DOWN move by row; ENTER picks and ESC cancels. The choice goes
 * to the editor's request in UiMemory, read when the editor is rebuilt.
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
        return tree;
    }

private:
    Screen tree;     ///< Screen of the picker
    PickerGrid grid; ///< Grid of characters
};

} // namespace ortxui

#endif /* ORTX_UI_PICKERVIEW_HPP */
