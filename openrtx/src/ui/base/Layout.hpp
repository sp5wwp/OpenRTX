/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_LAYOUT_HPP
#define ORTX_UI_LAYOUT_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>
#include "hwconfig.h"

namespace ortxui
{

/**
 * Main axis of a layout container.
 */
enum class Axis : uint8_t {
    Row,    ///< Children laid left to right
    Column, ///< Children laid top to bottom
};

/**
 * Distribution of free main-axis space among a container's children. Only
 * takes effect when no child grows: a grow weight takes the free space first,
 * as in CSS flexbox.
 */
enum class Justify : uint8_t {
    Start,        ///< Pack at the main-axis start
    Center,       ///< Pack centred
    End,          ///< Pack at the main-axis end
    SpaceBetween, ///< First and last at the edges, equal gaps between
    SpaceAround,  ///< Equal space around every child, half at the edges
};

/**
 * Cross-axis alignment of a child within its line. `Auto` on a child defers to
 * the container's default alignment.
 */
enum class Align : uint8_t {
    Auto,    ///< Child only: the container's cross alignment
    Start,   ///< Align to the cross-axis start
    Center,  ///< Centre on the cross axis
    End,     ///< Align to the cross-axis end
    Stretch, ///< Fill the cross axis
};

/**
 * A width and height in pixels: an object's natural size.
 */
struct Size {
    uint16_t w; ///< Width
    uint16_t h; ///< Height
};

/**
 * Per-child layout hints, read by a Flex parent when it positions the child.
 * The defaults describe a child fixed at its natural size, stretched across
 * the cross axis and with no margin: a no-op outside a layout container.
 */
struct LayoutHints {
    uint8_t grow = 0;         ///< Main-axis flex weight, 0 never grows
    int16_t basis = -1;       ///< Main-axis extent in px, -1 for natural()
    uint8_t margin = 0;       ///< Margin in px on every side of the child
    Align self = Align::Auto; ///< Cross-axis alignment override
};

/**
 * Size class of the panel. Views pick fonts and fixed extents by class, so one
 * layout serves both the small monochrome panels and the larger colour ones.
 * The classes grow in order: a view without a layout of its own for a class
 * uses the one of the class below, so views test for "at least Regular".
 */
enum class SizeClass : uint8_t {
    Compact, ///< Small panels, such as 128x64 mono
    Regular, ///< 160x128 colour panels
    Large,   ///< Tall colour panels, such as 240x320 portrait
};

/**
 * Get the size class of the panel.
 *
 * @return Large for panels 240 px tall or more, Regular for 128 px or more,
 * Compact otherwise.
 */
constexpr SizeClass sizeClass()
{
    return (CONFIG_SCREEN_HEIGHT >= 240) ? SizeClass::Large :
           (CONFIG_SCREEN_HEIGHT >= 128) ? SizeClass::Regular :
                                           SizeClass::Compact;
}

} // namespace ortxui

#endif /* ORTX_UI_LAYOUT_HPP */
