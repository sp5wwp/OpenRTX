/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/PickerGrid.hpp"
#include "core/Layout.hpp"
#include "render/DrawCtx.hpp"

namespace ortxui
{

/* Pickable code points: the Latin-1 supplement (accented letters + symbols),
 * matching the range baked into the custom icon/fallback font. */
static constexpr uint32_t kFirst = 0xA1;
static constexpr uint32_t kLast = 0xFF;
static constexpr uint16_t kCount = static_cast<uint16_t>(kLast - kFirst + 1);

/**
 * Get the code point of a grid cell.
 *
 * @param i: cell index.
 * @return the code point.
 */
static uint32_t codePointAt(uint16_t i)
{
    return kFirst + i;
}

/**
 * Encode a BMP code point to a NUL-terminated UTF-8 string.
 *
 * @param cp: code point.
 * @param out: output, at least 5 bytes.
 */
static void utf8(uint32_t cp, char *out)
{
    if (cp < 0x80u) {
        out[0] = static_cast<char>(cp);
        out[1] = '\0';
    } else if (cp < 0x800u) {
        out[0] = static_cast<char>(0xC0u | (cp >> 6));
        out[1] = static_cast<char>(0x80u | (cp & 0x3Fu));
        out[2] = '\0';
    } else {
        out[0] = static_cast<char>(0xE0u | (cp >> 12));
        out[1] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = static_cast<char>(0x80u | (cp & 0x3Fu));
        out[3] = '\0';
    }
}

/**
 * Get the height of a grid cell for the screen size.
 *
 * @return the height in pixels.
 */
static int16_t cellH()
{
    return (sizeClass() == SizeClass::Regular) ? 20 : 14;
}

uint16_t PickerGrid::cols() const
{
    return (sizeClass() == SizeClass::Regular) ? 8u : 6u;
}

uint32_t PickerGrid::selectedCodePoint() const
{
    return codePointAt(sel_);
}

void PickerGrid::ensureVisible()
{
    const uint16_t c = cols();
    const uint16_t row = static_cast<uint16_t>(sel_ / c);
    const uint16_t visRows =
        static_cast<uint16_t>(area_.h / static_cast<uint16_t>(cellH()));
    if (row < top_)
        top_ = row;
    else if ((visRows > 0) && (row >= top_ + visRows))
        top_ = static_cast<uint16_t>(row - visRows + 1);
}

void PickerGrid::move(int delta)
{
    int idx = static_cast<int>(sel_) + delta;
    if (idx < 0)
        idx = 0;
    if (idx >= static_cast<int>(kCount))
        idx = kCount - 1;
    sel_ = static_cast<uint16_t>(idx);
    ensureVisible();
    invalidate();
}

void PickerGrid::draw(DrawCtx &d)
{
    const uint16_t c = cols();
    const int16_t cw = static_cast<int16_t>(area_.w / c);
    const int16_t ch = cellH();
    const fontSize_t font = (sizeClass() == SizeClass::Regular) ?
                                FONT_SIZE_12PT :
                                FONT_SIZE_8PT;
    const uint16_t visRows =
        static_cast<uint16_t>(area_.h / static_cast<uint16_t>(ch));

    for (uint16_t vr = 0; vr < visRows; vr++) {
        const uint16_t row = static_cast<uint16_t>(top_ + vr);
        for (uint16_t col = 0; col < c; col++) {
            const uint16_t idx = static_cast<uint16_t>(row * c + col);
            if (idx >= kCount)
                return;
            const Rect cell = { static_cast<int16_t>(area_.x + col * cw),
                                static_cast<int16_t>(area_.y + vr * ch),
                                static_cast<uint16_t>(cw),
                                static_cast<uint16_t>(ch) };
            const bool selected = (idx == sel_);
            if (selected)
                d.fillRect(cell, Sem::Primary);
            char g[5];
            utf8(codePointAt(idx), g);
            d.textInBox(cell, font, TEXT_ALIGN_CENTER,
                        selected ? Sem::OnPrimary : Sem::OnSurface, g);
        }
    }
}

} // namespace ortxui
