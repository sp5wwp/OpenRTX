/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/PickerGrid.hpp"
#include "base/Layout.hpp"
#include "core/utils.h"
#include "render/DrawCtx.hpp"

namespace ortxui
{

/* Pickable code points: the Latin-1 supplement (accented letters + symbols),
 * matching the range baked into the custom icon/fallback font. */
static constexpr uint32_t FIRST_CP = 0xA1; ///< First pickable code point
static constexpr uint32_t LAST_CP = 0xFF;  ///< Last pickable code point
static constexpr uint16_t CELL_COUNT =
    static_cast<uint16_t>(LAST_CP - FIRST_CP + 1); ///< Number of cells

static constexpr int16_t CELL_H_REGULAR = 20; ///< Cell height, regular screen
static constexpr int16_t CELL_H_COMPACT = 14; ///< Cell height, compact screen
static constexpr uint16_t COLS_REGULAR = 8;   ///< Columns, regular screen
static constexpr uint16_t COLS_COMPACT = 6;   ///< Columns, compact screen

/**
 * Get the code point of a grid cell.
 *
 * @param i: cell index.
 * @return the code point.
 */
static uint32_t codePointAt(uint16_t i)
{
    return FIRST_CP + i;
}

/**
 * Get the height of a grid cell for the screen size.
 *
 * @return the height in pixels.
 */
static int16_t cellH()
{
    return (sizeClass() == SizeClass::Regular) ? CELL_H_REGULAR :
                                                 CELL_H_COMPACT;
}

uint16_t PickerGrid::cols() const
{
    return (sizeClass() == SizeClass::Regular) ? COLS_REGULAR : COLS_COMPACT;
}

uint32_t PickerGrid::selectedCodePoint() const
{
    return codePointAt(selectedCell);
}

void PickerGrid::ensureVisible()
{
    const uint16_t c = cols();
    const uint16_t row = static_cast<uint16_t>(selectedCell / c);
    const uint16_t visRows =
        static_cast<uint16_t>(bounds.h / static_cast<uint16_t>(cellH()));
    if (row < topRow)
        topRow = row;
    else if ((visRows > 0) && (row >= topRow + visRows))
        topRow = static_cast<uint16_t>(row - visRows + 1);
}

void PickerGrid::move(int delta)
{
    int idx = static_cast<int>(selectedCell) + delta;
    if (idx < 0)
        idx = 0;
    if (idx >= static_cast<int>(CELL_COUNT))
        idx = CELL_COUNT - 1;
    selectedCell = static_cast<uint16_t>(idx);
    ensureVisible();
    invalidate();
}

void PickerGrid::draw(DrawCtx &d)
{
    const uint16_t c = cols();
    const int16_t cw = static_cast<int16_t>(bounds.w / c);
    const int16_t ch = cellH();
    const fontSize_t font = (sizeClass() == SizeClass::Regular) ?
                                FONT_SIZE_12PT :
                                FONT_SIZE_8PT;
    const uint16_t visRows =
        static_cast<uint16_t>(bounds.h / static_cast<uint16_t>(ch));

    for (uint16_t vr = 0; vr < visRows; vr++) {
        const uint16_t row = static_cast<uint16_t>(topRow + vr);
        for (uint16_t col = 0; col < c; col++) {
            const uint16_t idx = static_cast<uint16_t>(row * c + col);
            if (idx >= CELL_COUNT)
                return;
            const Rect cell = { static_cast<int16_t>(bounds.x + col * cw),
                                static_cast<int16_t>(bounds.y + vr * ch),
                                static_cast<uint16_t>(cw),
                                static_cast<uint16_t>(ch) };
            const bool selected = (idx == selectedCell);
            if (selected)
                d.fillRect(cell, Sem::Primary);
            char g[4];
            g[utf8Encode(codePointAt(idx), g)] = '\0';
            d.textInBox(cell, font, TEXT_ALIGN_CENTER,
                        selected ? Sem::OnPrimary : Sem::OnSurface, g);
        }
    }
}

} // namespace ortxui
