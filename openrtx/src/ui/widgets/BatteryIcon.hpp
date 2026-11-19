/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_BATTERYICON_HPP
#define ORTX_UI_BATTERYICON_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>
#include "render/Geometry.hpp"
#include "base/Layout.hpp"

namespace ortxui
{

class DrawCtx;

/**
 * A small battery glyph for the top bar: an outlined body with a terminal nub
 * and an inner fill proportional to charge. The fill turns to the warning
 * colour at low charge. The top bar paints it in the slot it lays out, so the
 * glyph keeps no state of its own.
 */
class BatteryIcon
{
public:
    /** Constructor. */
    BatteryIcon() = default;

    /** Destructor. */
    ~BatteryIcon() = default;

    /**
     * Get the glyph's footprint, which the top bar centres in its slot.
     *
     * @return the size.
     */
    static Size size();

    /**
     * Paint the glyph within an area.
     *
     * @param d: drawing context.
     * @param area: area to fit the glyph in.
     * @param charge: battery charge in percent; above 100 counts as 100.
     */
    static void paint(DrawCtx &d, const Rect &area, uint8_t charge);
};

} // namespace ortxui

#endif /* ORTX_UI_BATTERYICON_HPP */
