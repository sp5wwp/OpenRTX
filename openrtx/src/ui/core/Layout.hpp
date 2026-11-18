/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_LAYOUT_HPP
#define ORTX_UI_LAYOUT_HPP

#include <cstdint>
#include "hwconfig.h"

namespace ortxui
{

/**
 * Main axis of a layout container.
 */
enum class Axis : uint8_t {
    Row,    //< children laid left-to-right; main axis is horizontal
    Column, //< children laid top-to-bottom; main axis is vertical
};

/**
 * Distribution of free main-axis space among a container's children. Only
 * takes effect when no child grows (a grow weight consumes the free space
 * first, just like CSS flexbox).
 */
enum class Justify : uint8_t {
    Start,        //< pack at the main-axis start
    Center,       //< pack centred
    End,          //< pack at the main-axis end
    SpaceBetween, //< first/last at the edges, equal gaps between
    SpaceAround,  //< equal space around every child (half at the edges)
};

/**
 * Cross-axis alignment of a child within its line. `Auto` on a child defers to
 * the container's default alignment.
 */
enum class Align : uint8_t {
    Auto,    //< (child only) inherit the container's cross alignment
    Start,   //< align to the cross-axis start
    Center,  //< centre on the cross axis
    End,     //< align to the cross-axis end
    Stretch, //< fill the cross axis
};

/**
 * A width/height pair in pixels (an object's preferred/intrinsic size).
 */
struct Size {
    uint16_t w;
    uint16_t h;
};

/**
 * Per-child layout hints, read by a Flex/Grid parent when it positions the
 * child. The defaults describe a child that is fixed at its natural size,
 * stretched across the cross axis and has no margin — i.e. a no-op for any
 * object that is not inside a layout container.
 */
struct LayoutHints {
    uint8_t grow = 0;   //< main-axis flex weight; 0 = fixed (never grows)
    int16_t basis = -1; //< preferred main-axis extent (px); -1 = use natural()
    uint8_t margin = 0; //< uniform margin (px) reserved around the child
    Align self = Align::Auto; //< cross-axis alignment override
};

/**
 * Responsive size class derived from the panel dimensions. Views pick fonts and
 * fixed extents by class so one layout serves both the small monochrome panels
 * and the larger colour ones.
 */
enum class SizeClass : uint8_t {
    Compact, //< small panels (e.g. 128x64 mono)
    Regular, //< 160x128 and larger colour panels
};

constexpr SizeClass sizeClass()
{
    return (CONFIG_SCREEN_HEIGHT >= 128) ? SizeClass::Regular :
                                           SizeClass::Compact;
}

/* Keypad input capability, from the target hwconfig. Text entry adapts to it:
 * a numeric keypad gets ETSI phone-style multi-tap; cursor movement uses the
 * LEFT/RIGHT arrows when present, otherwise the knob / UP-DOWN. */
constexpr bool kbdHasArrows()
{
    return CONFIG_KBD_HAS_ARROWS != 0;
}
constexpr bool kbdHasNumeric()
{
    return CONFIG_KBD_HAS_NUMERIC != 0;
}
constexpr bool kbdSpaceOnHash()
{
    return CONFIG_KBD_SPACE_ON_HASH != 0;
}

} // namespace ortxui

#endif /* ORTX_UI_LAYOUT_HPP */
