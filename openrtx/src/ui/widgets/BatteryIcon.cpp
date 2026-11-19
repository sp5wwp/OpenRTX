/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/BatteryIcon.hpp"
#include "render/DrawCtx.hpp"
#include "core/Layout.hpp"

namespace ortxui
{

Size BatteryIcon::size()
{
    /* An upright battery with the terminal on top. The height is kept a couple
     * of pixels short of the top-bar height so the glyph has padding above and
     * below; paint() fits within whatever area the row assigns. */
    const bool regular = (sizeClass() == SizeClass::Regular);
    return regular ? Size{ 13, 12 } : Size{ 10, 9 };
}

void BatteryIcon::paint(DrawCtx &d, const Rect &area, uint8_t charge)
{
    if (area.empty())
        return;

    if (charge > 100u)
        charge = 100u;

    const int16_t nubH = 2;
    const int16_t bodyH = static_cast<int16_t>(area.h - nubH);
    if (bodyH < 5)
        return;

    /* Battery aspect: taller than wide. Centre it in the assigned width. */
    int16_t W = static_cast<int16_t>(bodyH * 4 / 5);
    if (W > static_cast<int16_t>(area.w))
        W = static_cast<int16_t>(area.w);
    if (W < 6)
        W = 6;

    const int16_t bx = static_cast<int16_t>(area.x + (area.w - W) / 2);
    const int16_t byBody = static_cast<int16_t>(area.y + nubH);
    const uint16_t r = (W >= 9) ? 2u : 1u;

    /* Terminal: a SQUARED-off cap centred on top of the body (about three
     * fifths of the body width, crisp corners so it reads as a battery contact,
     * not a rounded bottle neck). It overlaps the body by 1px so they join. */
    const int16_t nubW = static_cast<int16_t>((W * 3) / 5);
    d.fillRect({ static_cast<int16_t>(bx + (W - nubW) / 2), area.y,
                 static_cast<uint16_t>(nubW), static_cast<uint16_t>(nubH + 1) },
               Sem::OnSurface);

    /* Body outline = an outer rounded rect hollowed by an inner one in the bar
     * colour (the top bar's Surface). The TOP corners are then squared off (a
     * real battery has a flat top under its terminal); the bottom keeps its
     * anti-aliased rounding. */
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

    /* Charge fill, from the bottom up, inset 2px from the outline. */
    const int innerH = bodyH - 4;
    const int fillH = innerH * static_cast<int>(charge) / 100;
    if (fillH <= 0)
        return;

    const Sem fill = (charge <= 20u) ? Sem::Warning : Sem::OnSurface;
    d.fillRoundRect({ static_cast<int16_t>(bx + 2),
                      static_cast<int16_t>(byBody + 2 + (innerH - fillH)),
                      static_cast<uint16_t>(W - 4),
                      static_cast<uint16_t>(fillH) },
                    1, fill);
}

} // namespace ortxui
