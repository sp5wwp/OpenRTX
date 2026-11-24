/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/BatteryIcon.hpp"
#include "render/DrawCtx.hpp"
#include "base/Layout.hpp"

#include <algorithm>

namespace ortxui
{

static constexpr int16_t NUB_W = 2;      ///< Width of the terminal
static constexpr int16_t MIN_BODY_W = 8; ///< Narrowest body that reads as one
static constexpr int16_t MIN_H = 5;      ///< Lowest body

static constexpr int16_t BODY_H_NUM = 3; ///< Body height per width, numerator
static constexpr int16_t BODY_H_DEN = 5; ///< Body height per width, divisor
static constexpr int16_t NUB_H_NUM = 3; ///< Terminal height per body, numerator
static constexpr int16_t NUB_H_DEN = 5; ///< Terminal height per body, divisor

/** Body height from which the corners round by 2 px rather than 1. */
static constexpr int16_t ROUND2_H = 9;

static constexpr int16_t FILL_INSET = 2;   ///< Inset of the fill in the outline
static constexpr uint16_t FILL_RADIUS = 1; ///< Corner radius of the fill

/** Footprint on a large screen. */
static constexpr Size SIZE_LARGE = { 24, 12 };

/** Footprint on a regular screen, short of the bar height to leave a margin. */
static constexpr Size SIZE_REGULAR = { 20, 10 };

/** Footprint on a compact screen. */
static constexpr Size SIZE_COMPACT = { 15, 8 };

/** Charge at or below which the fill shows the warning colour. */
static constexpr uint8_t LOW_CHARGE = 20;

Size BatteryIcon::size()
{
    if (sizeClass() >= SizeClass::Large)
        return SIZE_LARGE;

    return (sizeClass() >= SizeClass::Regular) ? SIZE_REGULAR : SIZE_COMPACT;
}

void BatteryIcon::paint(DrawCtx &d, const Rect &area, uint8_t charge)
{
    if (area.empty())
        return;

    charge = std::min<uint8_t>(charge, 100u);

    const int16_t bodyW = static_cast<int16_t>(area.w - NUB_W);
    if (bodyW < MIN_BODY_W)
        return;

    /* Wider than tall, lying on its side, centred in the assigned height. */
    const int16_t H =
        std::max(std::min(static_cast<int16_t>(bodyW * BODY_H_NUM / BODY_H_DEN),
                          static_cast<int16_t>(area.h)),
                 MIN_H);

    const int16_t bx = area.x;
    const int16_t by = static_cast<int16_t>(area.y + (area.h - H) / 2);
    const int16_t right = static_cast<int16_t>(bx + bodyW);
    const uint16_t r = (H >= ROUND2_H) ? 2u : 1u;

    /* A square terminal on the right, overlapping the body by 1 px so they
     * join. */
    const int16_t nubH = static_cast<int16_t>((H * NUB_H_NUM) / NUB_H_DEN);
    d.fillRect({ static_cast<int16_t>(right - 1),
                 static_cast<int16_t>(by + (H - nubH) / 2),
                 static_cast<uint16_t>(NUB_W + 1),
                 static_cast<uint16_t>(nubH) },
               Sem::OnSurface);

    /* The outline: a body with rounded corners all round, hollowed in the
     * bar colour. */
    d.fillRoundRect({ bx, by, static_cast<uint16_t>(bodyW),
                      static_cast<uint16_t>(H) },
                    r, Sem::OnSurface);

    const uint16_t ir = (r > 0u) ? static_cast<uint16_t>(r - 1u) : 0u;
    d.fillRoundRect(
        { static_cast<int16_t>(bx + 1), static_cast<int16_t>(by + 1),
          static_cast<uint16_t>(bodyW - 2), static_cast<uint16_t>(H - 2) },
        ir, Sem::Surface);

    /* The charge fills from the left. */
    const int innerW = bodyW - 2 * FILL_INSET;
    const int fillW = innerW * static_cast<int>(charge) / 100;
    if (fillW <= 0)
        return;

    const Sem fill = (charge <= LOW_CHARGE) ? Sem::Warning : Sem::OnSurface;
    d.fillRoundRect({ static_cast<int16_t>(bx + FILL_INSET),
                      static_cast<int16_t>(by + FILL_INSET),
                      static_cast<uint16_t>(fillW),
                      static_cast<uint16_t>(H - 2 * FILL_INSET) },
                    FILL_RADIUS, fill);
}

} // namespace ortxui
