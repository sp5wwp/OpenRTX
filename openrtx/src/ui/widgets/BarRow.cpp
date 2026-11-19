/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/BarRow.hpp"
#include "core/utils.h"
#include "render/DrawCtx.hpp"

#include <cmath>
#include <cstdio>

namespace ortxui
{

static constexpr int16_t PAD = 3;                       ///< Outer gutter
static constexpr int16_t GAP = 5;                       ///< Gap by the texts
static constexpr size_t LABEL_SIZE = 32;                ///< Joined label
static constexpr fontSize_t SCALE_FONT = FONT_SIZE_5PT; ///< S-scale numbers
static constexpr float S_FULL = 11.0f;                  ///< S-point at full

/* S-points labelled beneath the track, placed by S / S_FULL on its axis. */
static constexpr uint8_t SCALE_POINTS[] = { 1, 3, 5, 7, 9 };

/* Box of a scale number, centred on its tick. */
static constexpr int16_t NUM_HALF_W = 4;
static constexpr uint16_t NUM_W = 9;

/* Width of the mark, and how far it stands out of the track. */
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

Size BarRow::natural() const
{
    const uint8_t numH = showScale_ ? gfx_getFontHeight(SCALE_FONT) : 0;
    return { 0, static_cast<uint16_t>(gfx_getFontLineHeight(font_) + numH) };
}

const char *BarRow::labelText(char *buf, size_t n) const
{
    const char *label = label_.str();
    if (icon_ == nullptr)
        return label;

    sniprintf(buf, n, "%s %s", icon_, label);
    return buf;
}

uint16_t BarRow::track(int16_t &left) const
{
    char buf[LABEL_SIZE];
    const char *label = labelText(buf, sizeof(buf));

    left = static_cast<int16_t>(area_.x + PAD);
    int16_t right = static_cast<int16_t>(area_.x + area_.w - PAD);
    if (label[0] != '\0')
        left =
            static_cast<int16_t>(left + gfx_getTextWidth(font_, label) + GAP);
    if (readout_[0] != '\0')
        right = static_cast<int16_t>(right - gfx_getTextWidth(font_, readout_)
                                     - GAP);

    return (right > left) ? static_cast<uint16_t>(right - left) : 0;
}

uint16_t BarRow::fillWidth() const
{
    int16_t left;
    return static_cast<uint16_t>(std::lround(track(left) * clamp01(value_)));
}

void BarRow::draw(DrawCtx &d)
{
    /* The scale numbers take a band beneath the track; the texts and the
     * track centre on the rest. */
    const uint16_t numH = showScale_ ? gfx_getFontHeight(SCALE_FONT) : 0;
    const uint16_t bandH = (area_.h > numH) ?
                               static_cast<uint16_t>(area_.h - numH) :
                               0;

    /* Label pinned to the left, readout to the right. */
    char buf[LABEL_SIZE];
    const char *label = labelText(buf, sizeof(buf));
    if (label[0] != '\0') {
        const uint16_t lw = gfx_getTextWidth(font_, label);
        d.textInBox({ static_cast<int16_t>(area_.x + PAD), area_.y, lw, bandH },
                    font_, TEXT_ALIGN_LEFT, text_, label);
    }
    if (readout_[0] != '\0') {
        const uint16_t rw = gfx_getTextWidth(font_, readout_);
        d.textInBox({ static_cast<int16_t>(area_.x + area_.w - PAD - rw),
                      area_.y, rw, bandH },
                    font_, TEXT_ALIGN_RIGHT, text_, readout_);
    }

    int16_t left;
    const uint16_t trackW = track(left);
    if (trackW == 0)
        return;

    /* A rounded track between the texts, filled to the value. */
    const uint16_t barH = (bandH < barH_) ? bandH : barH_;
    const int16_t barY = static_cast<int16_t>(area_.y + (bandH - barH) / 2);
    const uint16_t radius = static_cast<uint16_t>(barH / 2);

    d.fillRoundRect({ left, barY, trackW, barH }, radius, track_);

    const uint16_t fillW = fillWidth();
    if (fillW > 0)
        d.fillRoundRect({ left, barY, fillW, barH }, radius, fill_);

    /* S-scale: a faint tick under the track and the number beneath it. */
    if (showScale_ && (numH > 0)) {
        const int16_t numY = static_cast<int16_t>(area_.y + bandH);
        for (size_t i = 0; i < ARRAY_SIZE(SCALE_POINTS); i++) {
            const float frac = SCALE_POINTS[i] / S_FULL;
            const int16_t x =
                static_cast<int16_t>(left + std::lround(frac * trackW));
            d.fillRect({ x, static_cast<int16_t>(barY + barH), 1, 1 },
                       Sem::OnSurfaceMuted);
            char num[3];
            sniprintf(num, sizeof(num), "%u",
                      static_cast<unsigned>(SCALE_POINTS[i]));
            d.textInBox(
                { static_cast<int16_t>(x - NUM_HALF_W), numY, NUM_W, numH },
                SCALE_FONT, TEXT_ALIGN_CENTER, Sem::OnSurfaceMuted, num);
        }
    }

    /* The mark is drawn over the fill, so it shows on either side of it. */
    if (marker_ >= 0.0f) {
        const int16_t mx =
            static_cast<int16_t>(left + std::lround(trackW * clamp01(marker_)));
        const int16_t top = static_cast<int16_t>(barY - MARK_OUT);
        const uint16_t h = static_cast<uint16_t>(barH + 2 * MARK_OUT);
        d.fillRect({ mx, top, MARK_W, h }, Sem::TxDanger);
    }
}

} // namespace ortxui
