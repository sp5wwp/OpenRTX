/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_GEOMETRY_HPP
#define ORTX_UI_GEOMETRY_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>

namespace ortxui
{

/**
 * A point in screen space, in pixel coordinates.
 */
struct Point {
    int16_t x; ///< Column
    int16_t y; ///< Row
};

/**
 * An axis-aligned rectangle, origin at its top-left corner.
 */
struct Rect {
    /** Last column inside the rectangle. */
    constexpr int16_t right() const
    {
        return static_cast<int16_t>(x + static_cast<int16_t>(w) - 1);
    }

    /** Last row inside the rectangle. */
    constexpr int16_t bottom() const
    {
        return static_cast<int16_t>(y + static_cast<int16_t>(h) - 1);
    }

    /** True when the rectangle covers no pixel. */
    constexpr bool empty() const
    {
        return (w == 0) || (h == 0);
    }

    int16_t x;  ///< Left column
    int16_t y;  ///< Top row
    uint16_t w; ///< Width, in pixels
    uint16_t h; ///< Height, in pixels
};

/**
 * Compare two rectangles.
 *
 * @param a: first rectangle.
 * @param b: second rectangle.
 * @return true when they have the same corner and size.
 */
constexpr bool operator==(const Rect &a, const Rect &b)
{
    return (a.x == b.x) && (a.y == b.y) && (a.w == b.w) && (a.h == b.h);
}

/**
 * Intersect two rectangles.
 *
 * @param a: first rectangle.
 * @param b: second rectangle.
 * @return the overlap, or an empty rectangle when they do not overlap.
 */
inline Rect intersect(const Rect &a, const Rect &b)
{
    const int16_t x0 = (a.x > b.x) ? a.x : b.x;
    const int16_t y0 = (a.y > b.y) ? a.y : b.y;
    const int16_t x1 = (a.right() < b.right()) ? a.right() : b.right();
    const int16_t y1 = (a.bottom() < b.bottom()) ? a.bottom() : b.bottom();

    if ((x1 < x0) || (y1 < y0))
        return Rect{ 0, 0, 0, 0 };

    return Rect{ x0, y0, static_cast<uint16_t>(x1 - x0 + 1),
                 static_cast<uint16_t>(y1 - y0 + 1) };
}

} // namespace ortxui

#endif /* ORTX_UI_GEOMETRY_HPP */
