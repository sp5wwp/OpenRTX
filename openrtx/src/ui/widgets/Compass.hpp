/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_COMPASS_HPP
#define ORTX_UI_COMPASS_HPP

#include <cstdint>
#include "core/Object.hpp"

namespace ortxui
{

/**
 * A small compass rose (GPS mockup 1): an outlined dial with N/E/S/W cardinals,
 * an accent-coloured needle pointing to the current course-over-ground, and the
 * heading in degrees below the dial. When there is no fix the needle is hidden
 * and the dial reads as an inert placeholder.
 */
class Compass : public Object
{
public:
    /** Heading 0-359 (0 = North, clockwise); `valid` gates the needle. */
    void setHeading(int16_t deg, bool valid);

    void draw(DrawCtx &d) override;

private:
    int16_t heading_ = 0;
    bool valid_ = false;
    char label_[6] = "---";
};

} // namespace ortxui

#endif /* ORTX_UI_COMPASS_HPP */
