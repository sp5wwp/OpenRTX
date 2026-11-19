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

/*
 * Keypad keys, as presence macros in the target's hwconfig.h; a target
 * without the keys leaves the macro undefined. Text entry adapts to them:
 * - CONFIG_KBD_HAS_ARROWS: LEFT/RIGHT move the cursor; without them the knob
 *   or UP/DOWN do.
 * - CONFIG_KBD_HAS_NUMERIC: 0-9 keys, typed with ETSI phone-style multi-tap.
 * - CONFIG_KBD_SPACE_ON_HASH: the keypad prints space on '#' rather than '0'.
 * The UI reads them only through these functions.
 */

/** True when the keypad has LEFT/RIGHT keys. */
constexpr bool kbdHasArrows()
{
#ifdef CONFIG_KBD_HAS_ARROWS
    return true;
#else
    return false;
#endif
}

/** True when the keypad has 0-9 keys. */
constexpr bool kbdHasNumeric()
{
#ifdef CONFIG_KBD_HAS_NUMERIC
    return true;
#else
    return false;
#endif
}

/** True when the keypad prints space on '#' rather than '0'. */
constexpr bool kbdSpaceOnHash()
{
#ifdef CONFIG_KBD_SPACE_ON_HASH
    return true;
#else
    return false;
#endif
}

} // namespace ortxui

#endif /* ORTX_UI_LAYOUT_HPP */
