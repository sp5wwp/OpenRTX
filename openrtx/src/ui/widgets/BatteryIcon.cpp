/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/BatteryIcon.hpp"
#include "render/DrawCtx.hpp"
#include "base/Layout.hpp"

namespace ortxui
{

/* Height of the terminal, and the smallest body that still reads as one. */
static constexpr int16_t NUB_H = 2;
static constexpr int16_t MIN_BODY_H = 5;
static constexpr int16_t MIN_W = 6;

/* Width of the body for its height, and of the terminal for the body's. */
static constexpr int16_t BODY_W_NUM = 4;
static constexpr int16_t BODY_W_DEN = 5;
static constexpr int16_t NUB_W_NUM = 3;
static constexpr int16_t NUB_W_DEN = 5;

/* Body width from which the corners round by 2 px rather than 1. */
static constexpr int16_t ROUND2_W = 9;

/* Inset of the charge fill from the outline. */
static constexpr int16_t FILL_INSET = 2;

/* Corner radius of the charge fill. */
static constexpr uint16_t FILL_RADIUS = 1;

/* Glyph footprint, a little shorter than the top bar so it has room above and
 * below. */
static constexpr Size SIZE_REGULAR = { 13, 12 };
static constexpr Size SIZE_COMPACT = { 10, 9 };

/* Charge at or below which the fill shows the warning colour. */
static constexpr uint8_t LOW_CHARGE = 20;

Size BatteryIcon::size()
{
    return (sizeClass() == SizeClass::Regular) ? SIZE_REGULAR : SIZE_COMPACT;
}

void BatteryIcon::paint(DrawCtx &d, const Rect &area, uint8_t charge)
{
    if (area.empty())
        return;

    if (charge > 100u)
        charge = 100u;

    const int16_t bodyH = static_cast<int16_t>(area.h - NUB_H);
    if (bodyH < MIN_BODY_H)
        return;

    /* Taller than wide, centred in the assigned width. */
    int16_t W = static_cast<int16_t>(bodyH * BODY_W_NUM / BODY_W_DEN);
    if (W > static_cast<int16_t>(area.w))
        W = static_cast<int16_t>(area.w);
    if (W < MIN_W)
        W = MIN_W;

    const int16_t bx = static_cast<int16_t>(area.x + (area.w - W) / 2);
    const int16_t byBody = static_cast<int16_t>(area.y + NUB_H);
    const uint16_t r = (W >= ROUND2_W) ? 2u : 1u;

    /* A square terminal centred on top, overlapping the body by 1 px so they
     * join. */
    const int16_t nubW = static_cast<int16_t>((W * NUB_W_NUM) / NUB_W_DEN);
    d.fillRect({ static_cast<int16_t>(bx + (W - nubW) / 2), area.y,
                 static_cast<uint16_t>(nubW),
                 static_cast<uint16_t>(NUB_H + 1) },
               Sem::OnSurface);

    /* The outline: a rounded body hollowed in the bar colour, with square top
     * corners under the terminal. */
    d.fillRoundRect({ bx, byBody, static_cast<uint16_t>(W),
                      static_cast<uint16_t>(bodyH) },
                    r, Sem::OnSurface);
    d.fillRect({ bx, byBody, r, r }, Sem::OnSurface);
    d.fillRect({ static_cast<int16_t>(bx + W - r), byBody, r, r },
               Sem::OnSurface);

    const uint16_t ir = (r > 0u) ? static_cast<uint16_t>(r - 1u) : 0u;
    d.fillRoundRect(
        { static_cast<int16_t>(bx + 1), static_cast<int16_t>(byBody + 1),
          static_cast<uint16_t>(W - 2), static_cast<uint16_t>(bodyH - 2) },
        ir, Sem::Surface);
    if (ir > 0u) {
        d.fillRect({ static_cast<int16_t>(bx + 1),
                     static_cast<int16_t>(byBody + 1), ir, ir },
                   Sem::Surface);
        d.fillRect({ static_cast<int16_t>(bx + W - 1 - ir),
                     static_cast<int16_t>(byBody + 1), ir, ir },
                   Sem::Surface);
    }

    /* The charge fills from the bottom up. */
    const int innerH = bodyH - 2 * FILL_INSET;
    const int fillH = innerH * static_cast<int>(charge) / 100;
    if (fillH <= 0)
        return;

    const Sem fill = (charge <= LOW_CHARGE) ? Sem::Warning : Sem::OnSurface;
    d.fillRoundRect(
        { static_cast<int16_t>(bx + FILL_INSET),
          static_cast<int16_t>(byBody + FILL_INSET + (innerH - fillH)),
          static_cast<uint16_t>(W - 2 * FILL_INSET),
          static_cast<uint16_t>(fillH) },
        FILL_RADIUS, fill);
}

} // namespace ortxui
