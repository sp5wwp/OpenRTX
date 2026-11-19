/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/SignalMeter.hpp"
#include "core/utils.h"
#include "render/DrawCtx.hpp"

#include <cmath>
#include <cstdio>

namespace ortxui
{

static constexpr int16_t PAD = 3;                       ///< Outer gutter
static constexpr int16_t GAP = 5;                       ///< Gap by the texts
static constexpr uint16_t BAR_H = 7;                    ///< Bar thickness
static constexpr fontSize_t FONT = FONT_SIZE_6PT;       ///< Label, readout
static constexpr fontSize_t SCALE_FONT = FONT_SIZE_5PT; ///< S-scale numbers
static constexpr float S_FULL = 11.0f;                  ///< S-point at full

/* S-points labelled beneath the bar, placed by S / S_FULL on its axis. */
static constexpr uint8_t SCALE_POINTS[] = { 1, 3, 5, 7, 9 };

/* Natural width and the space under the scale numbers. */
static constexpr uint16_t NATURAL_W = 200;
static constexpr uint16_t SCALE_PAD = 3;

/* Box of a scale number, centred on its tick. */
static constexpr int16_t NUM_HALF_W = 4;
static constexpr uint16_t NUM_W = 9;

/* Width of the mark, and how far it stands out of the bar. */
static constexpr uint16_t MARK_W = 2;
static constexpr int16_t MARK_OUT = 1;

/**
 * Clamp a value to [0, 1].
 *
 * @param v: value.
 * @return the clamped value.
 */
static float clamp01(float v)
{
    if (v < 0.0f)
        return 0.0f;
    if (v > 1.0f)
        return 1.0f;
    return v;
}

Size SignalMeter::natural() const
{
    return { NATURAL_W,
             static_cast<uint16_t>(BAR_H + gfx_getFontHeight(SCALE_FONT)
                                   + SCALE_PAD) };
}

uint16_t SignalMeter::track(int16_t &left) const
{
    left = static_cast<int16_t>(area_.x + PAD);
    int16_t right = static_cast<int16_t>(area_.x + area_.w - PAD);
    if (label_[0] != '\0')
        left =
            static_cast<int16_t>(left + gfx_getTextWidth(FONT, label_) + GAP);
    if (readout_[0] != '\0')
        right = static_cast<int16_t>(right - gfx_getTextWidth(FONT, readout_)
                                     - GAP);

    return (right > left) ? static_cast<uint16_t>(right - left) : 0;
}

uint16_t SignalMeter::fillWidth() const
{
    int16_t left;
    return static_cast<uint16_t>(std::lround(track(left) * clamp01(value_)));
}

void SignalMeter::draw(DrawCtx &d)
{
    /* Reserve a band beneath the bar for the S-scale numbers (RX only). */
    const uint16_t numH = showScale_ ? gfx_getFontHeight(SCALE_FONT) : 0;
    const int16_t barBandH = static_cast<int16_t>(area_.h - numH);

    /* Label and readout centre on the bar band (not the whole area, so the
     * scale numbers below don't drag them down). */
    if (label_[0] != '\0') {
        const uint16_t lw = gfx_getTextWidth(FONT, label_);
        d.textInBox({ static_cast<int16_t>(area_.x + PAD), area_.y, lw,
                      static_cast<uint16_t>(barBandH) },
                    FONT, TEXT_ALIGN_LEFT, color_, label_);
    }
    if (readout_[0] != '\0') {
        const uint16_t rw = gfx_getTextWidth(FONT, readout_);
        d.textInBox({ static_cast<int16_t>(area_.x + area_.w - PAD - rw),
                      area_.y, rw, static_cast<uint16_t>(barBandH) },
                    FONT, TEXT_ALIGN_RIGHT, color_, readout_);
    }

    int16_t left;
    const uint16_t trackW = track(left);
    if (trackW == 0)
        return;

    /* Bar geometry: a rounded track vertically centred in the bar band. */
    uint16_t barH = BAR_H;
    if (barH > static_cast<uint16_t>(barBandH))
        barH = static_cast<uint16_t>(barBandH);
    const int16_t barY = static_cast<int16_t>(
        area_.y + (barBandH - static_cast<int16_t>(barH)) / 2);
    const uint16_t radius = static_cast<uint16_t>(barH / 2);

    d.fillRoundRect({ left, barY, trackW, barH }, radius, Sem::SurfaceHigh);

    const uint16_t fillW =
        static_cast<uint16_t>(std::lround(trackW * clamp01(value_)));
    if (fillW > 0)
        d.fillRoundRect({ left, barY, fillW, barH }, radius, color_);

    /* S-scale: a faint tick under the bar plus the number, at S/11 positions. */
    if (showScale_ && (numH > 0)) {
        const int16_t numY = static_cast<int16_t>(area_.y + barBandH);
        for (size_t i = 0; i < ARRAY_SIZE(SCALE_POINTS); i++) {
            const float frac = SCALE_POINTS[i] / S_FULL;
            const int16_t x =
                static_cast<int16_t>(left + std::lround(frac * trackW));
            d.fillRect({ x, static_cast<int16_t>(barY + barH), 1, 1 },
                       Sem::OnSurfaceMuted);
            char num[3];
            sniprintf(num, sizeof(num), "%u", (unsigned)SCALE_POINTS[i]);
            d.textInBox(
                { static_cast<int16_t>(x - NUM_HALF_W), numY, NUM_W, numH },
                SCALE_FONT, TEXT_ALIGN_CENTER, Sem::OnSurfaceMuted, num);
        }
    }

    /* Squelch gate: a red vertical mark on the same axis, drawn over the fill so
     * it stays visible whether the signal is above or below it. */
    if (marker_ >= 0.0f) {
        const int16_t mx =
            static_cast<int16_t>(left + std::lround(trackW * clamp01(marker_)));
        const int16_t top = static_cast<int16_t>(barY - MARK_OUT);
        const uint16_t h = static_cast<uint16_t>(barH + 2 * MARK_OUT);
        d.fillRect({ mx, top, MARK_W, h }, Sem::TxDanger);
    }
}

} // namespace ortxui
