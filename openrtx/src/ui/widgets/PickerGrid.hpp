/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_PICKERGRID_HPP
#define ORTX_UI_PICKERGRID_HPP

#include <cstdint>
#include "base/Object.hpp"

namespace ortxui
{

/**
 * Grid of pickable code points from the icon font, with the selection and the
 * scroll row.
 */
class PickerGrid : public Object
{
public:
    /** Constructor. */
    PickerGrid() = default;

    /** Destructor. */
    ~PickerGrid() = default;

    /**
     * Put back a selection and scroll row.
     *
     * @param sel: selected cell.
     * @param top: first visible row.
     */
    void setPosition(uint16_t sel, uint16_t top)
    {
        selectedCell = sel;
        topRow = top;
    }

    /** Selected cell. */
    uint16_t selected() const
    {
        return selectedCell;
    }

    /** First visible row. */
    uint16_t top() const
    {
        return topRow;
    }

    /**
     * Step the selection, scrolling as needed.
     *
     * @param delta: cells to step; a row is cols() cells.
     */
    void move(int delta);

    /** Code point of the selected cell. */
    uint32_t selectedCodePoint() const;

    /** Cells in a row. */
    uint16_t cols() const;

    /** Paint the visible rows, the selection highlighted. */
    void draw(DrawCtx &d) override;

private:
    /** Scroll so the selected row is visible. */
    void ensureVisible();

    uint16_t selectedCell = 0; ///< Selected cell
    uint16_t topRow = 0;       ///< First visible row
};

} // namespace ortxui

#endif /* ORTX_UI_PICKERGRID_HPP */
