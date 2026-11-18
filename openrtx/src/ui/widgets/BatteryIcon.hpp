/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_BATTERYICON_HPP
#define ORTX_UI_BATTERYICON_HPP

#include <cstdint>
#include "core/Object.hpp"

namespace ortxui
{

/**
 * A small battery glyph for the top bar: an outlined body with a terminal nub
 * and an inner fill proportional to charge. The fill turns to the warning
 * colour at low charge. Drawn within its layout-assigned area; natural() gives
 * it a fixed footprint so a Flex row reserves the right width.
 */
class BatteryIcon : public Object
{
public:
    void setCharge(uint8_t pct)
    {
        charge_ = (pct > 100u) ? 100u : pct;
    }

    Size natural() const override;
    void draw(DrawCtx &d) override;

private:
    uint8_t charge_ = 0;
};

} // namespace ortxui

#endif /* ORTX_UI_BATTERYICON_HPP */
