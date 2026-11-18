/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/SignalMeter.hpp"
#include "render/DrawCtx.hpp"

#include <cmath>
#include <cstdio>

namespace ortxui
{

namespace
{
constexpr int16_t kPad = 3;                  //< outer gutter
constexpr int16_t kGap = 5;                  //< gap around the label / readout
constexpr uint16_t kBarH = 7;                //< preferred bar thickness
const fontSize_t kFont = FONT_SIZE_6PT;      //< label / readout
const fontSize_t kScaleFont = FONT_SIZE_5PT; //< S-scale numbers

/* S-points labelled beneath the bar; positioned by S/11 to match the axis. */
const uint8_t kScalePoints[] = { 1, 3, 5, 7, 9 };

float clamp01(float v)
{
    if (v < 0.0f)
        return 0.0f;
    if (v > 1.0f)
        return 1.0f;
    return v;
}
} // namespace

Size SignalMeter::natural() const
{
    return { 200,
             static_cast<uint16_t>(kBarH + gfx_getFontHeight(kScaleFont) + 3) };
}

void SignalMeter::draw(DrawCtx &d)
{
    int16_t left = static_cast<int16_t>(area_.x + kPad);
    int16_t right = static_cast<int16_t>(area_.x + area_.w - kPad);

    /* Reserve a band beneath the bar for the S-scale numbers (RX only). */
    const uint16_t numH = showScale_ ? gfx_getFontHeight(kScaleFont) : 0;
    const int16_t barBandH = static_cast<int16_t>(area_.h - numH);

    /* Label and readout centre on the bar band (not the whole area, so the
     * scale numbers below don't drag them down). */
    if (label_[0] != '\0') {
        const uint16_t lw = gfx_getTextWidth(kFont, label_);
        d.textInBox({ left, area_.y, lw, (uint16_t)barBandH }, kFont,
                    TEXT_ALIGN_LEFT, color_, label_);
        left = static_cast<int16_t>(left + lw + kGap);
    }
    if (readout_[0] != '\0') {
        const uint16_t rw = gfx_getTextWidth(kFont, readout_);
        d.textInBox({ static_cast<int16_t>(right - rw), area_.y, rw,
                      (uint16_t)barBandH },
                    kFont, TEXT_ALIGN_RIGHT, color_, readout_);
        right = static_cast<int16_t>(right - rw - kGap);
    }

    if (right <= left)
        return;
    const uint16_t trackW = static_cast<uint16_t>(right - left);

    /* Bar geometry: a rounded track vertically centred in the bar band. */
    uint16_t barH = kBarH;
    if (barH > (uint16_t)barBandH)
        barH = (uint16_t)barBandH;
    const int16_t barY =
        static_cast<int16_t>(area_.y + (barBandH - (int16_t)barH) / 2);
    const uint16_t radius = static_cast<uint16_t>(barH / 2);

    d.fillRoundRect({ left, barY, trackW, barH }, radius, Sem::SurfaceHigh);

    const uint16_t fillW =
        static_cast<uint16_t>(std::lround(trackW * clamp01(value_)));
    if (fillW > 0)
        d.fillRoundRect({ left, barY, fillW, barH }, radius, color_);

    /* S-scale: a faint tick under the bar plus the number, at S/11 positions. */
    if (showScale_ && numH > 0) {
        const int16_t numY = static_cast<int16_t>(area_.y + barBandH);
        for (uint8_t i = 0; i < sizeof(kScalePoints); i++) {
            const float frac = kScalePoints[i] / 11.0f;
            const int16_t x =
                static_cast<int16_t>(left + std::lround(frac * trackW));
            d.fillRect({ x, static_cast<int16_t>(barY + barH), 1, 1 },
                       Sem::OnSurfaceMuted);
            char num[3];
            snprintf(num, sizeof(num), "%u", (unsigned)kScalePoints[i]);
            d.textInBox({ static_cast<int16_t>(x - 4), numY, 9, numH },
                        kScaleFont, TEXT_ALIGN_CENTER, Sem::OnSurfaceMuted,
                        num);
        }
    }

    /* Squelch gate: a red vertical mark on the same axis, drawn over the fill so
     * it stays visible whether the signal is above or below it. */
    if (marker_ >= 0.0f) {
        const int16_t mx =
            static_cast<int16_t>(left + std::lround(trackW * clamp01(marker_)));
        const int16_t top = static_cast<int16_t>(barY - 1);
        const uint16_t h = static_cast<uint16_t>(barH + 2);
        d.fillRect({ mx, top, 2, h }, Sem::TxDanger);
    }
}

} // namespace ortxui
