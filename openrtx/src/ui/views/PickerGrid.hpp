/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_PICKERGRID_HPP
#define ORTX_UI_PICKERGRID_HPP

#include <cstdint>
#include "core/Object.hpp"

namespace ortxui
{

/**
 * The grid of pickable code points (a leaf object so the Screen paints it).
 * Holds the selection + vertical scroll and knows how to navigate and paint a
 * grid of glyphs from the custom icon/fallback font.
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
        sel_ = sel;
        top_ = top;
    }

    /** Selected cell. */
    uint16_t selected() const
    {
        return sel_;
    }

    /** First visible row. */
    uint16_t top() const
    {
        return top_;
    }

    void move(int delta); //< step the selection, scrolling as needed
    uint32_t selectedCodePoint() const;

    void draw(DrawCtx &d) override;

private:
    uint16_t cols() const;
    void ensureVisible();

    uint16_t sel_ = 0;
    uint16_t top_ = 0; //< first visible row
};

} // namespace ortxui

#endif /* ORTX_UI_PICKERGRID_HPP */
