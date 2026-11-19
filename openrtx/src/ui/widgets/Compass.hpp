/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_COMPASS_HPP
#define ORTX_UI_COMPASS_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>
#include "base/Object.hpp"

namespace ortxui
{

/**
 * A small compass rose: an outlined dial with N/E/S/W cardinals, a needle
 * pointing to the course over ground, and the heading in degrees below the
 * dial. Without a valid heading the needle is hidden.
 */
class Compass : public Object
{
public:
    /** Constructor. */
    Compass() = default;

    /** Destructor. */
    ~Compass() = default;

    /**
     * Set the heading.
     *
     * @param deg: degrees clockwise from north, any range.
     * @param valid: whether the needle shows.
     */
    void setHeading(int16_t deg, bool valid);

    /** Paint the dial, the needle and the heading. */
    void draw(DrawCtx &d) override;

private:
    uint16_t heading = 0;      ///< Heading, 0-359 degrees
    bool headingValid = false; ///< The needle shows
};

} // namespace ortxui

#endif /* ORTX_UI_COMPASS_HPP */
