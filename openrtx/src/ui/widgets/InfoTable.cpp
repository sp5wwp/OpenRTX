/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/InfoTable.hpp"
#include "base/Layout.hpp"
#include "base/UiText.hpp"
#include "core/memory_profiling.h"
#include "interfaces/platform.h"
#include "render/DrawCtx.hpp"
#include "hwconfig.h"

#include <climits>
#include <cstdio>

namespace ortxui
{

/* Row keys, in row order. */
static constexpr UiText KEYS[InfoTable::ROW_COUNT] = {
    STR(batteryVoltage), STR(batteryCharge), STR(RSSI),
    STR(usedHeap),       STR(band),          STR(hwVersion),
};

/* Inset of the key and value from the row edges. */
static constexpr int16_t PAD_X = 4;

/* Size of a composed value. */
static constexpr size_t VALUE_SIZE = 20;

/* Text font of the table. */
static constexpr fontSize_t FONT = (sizeClass() == SizeClass::Regular) ?
                                       FONT_SIZE_8PT :
                                       FONT_SIZE_6PT;

/**
 * Paint a text in its box, clipped to it.
 *
 * @param d: drawing context.
 * @param box: text box.
 * @param align: horizontal alignment.
 * @param color: text colour.
 * @param text: text.
 */
static void paintText(DrawCtx &d, const Rect &box, textAlign_t align, Sem color,
                      const char *text)
{
    const bool clip = !box.empty();
    if (clip)
        d.pushClip(box);
    d.textInBox(box, FONT, align, color, text);
    if (clip)
        d.popClip();
}

InfoTable::InfoTable() :
    heapUsed_(UINT32_MAX), rssi_(INT32_MIN), vbat_(UINT16_MAX),
    charge_(UINT8_MAX)
{
}

void InfoTable::composeValue(uint8_t row, char *out, size_t n) const
{
    const hwInfo_t *hw = platform_getHwInfo();

    switch (row) {
        case ROW_BATTERY: {
            /* Integer volts + one decimal, rounding the mantissa to nearest. */
            const uint16_t volt = (vbat_ + 50) / 1000;
            const uint16_t mv = ((vbat_ - volt * 1000) + 50) / 100;
            sniprintf(out, n, "%u.%uV", volt, mv);
            break;
        }
        case ROW_CHARGE:
            sniprintf(out, n, "%u%%", charge_);
            break;
        case ROW_RSSI:
            sniprintf(out, n, "%ld dBm", static_cast<long>(rssi_));
            break;
        case ROW_HEAP:
            sniprintf(out, n, "%lu B", static_cast<unsigned long>(heapUsed_));
            break;
        case ROW_BAND:
            sniprintf(out, n, "%s%s", hw->vhf_band ? "VHF " : "",
                      hw->uhf_band ? "UHF" : "");
            break;
        default:
            sniprintf(out, n, "%u", hw->hw_version);
            break;
    }
}

bool InfoTable::sync(const state_t &s)
{
    const uint32_t used = getHeapSize() - getCurrentFreeHeap();
    const bool changed = (s.v_bat != vbat_) || (s.charge != charge_)
                      || (s.rssi != rssi_) || (used != heapUsed_);

    vbat_ = s.v_bat;
    charge_ = s.charge;
    rssi_ = s.rssi;
    heapUsed_ = used;
    return changed;
}

void InfoTable::draw(DrawCtx &d)
{
    /* The rows split the height evenly. */
    const uint16_t rowH = static_cast<uint16_t>(area_.h / ROW_COUNT);
    for (uint8_t i = 0; i < ROW_COUNT; i++) {
        const Rect row = { area_.x, static_cast<int16_t>(area_.y + i * rowH),
                           area_.w, rowH };
        /* A row outside the clip, as in another band, draws nothing. */
        if (!row.empty() && intersect(d.clip(), row).empty())
            continue;
        drawRow(d, i, row);
    }
}

void InfoTable::drawRow(DrawCtx &d, uint8_t i, const Rect &row) const
{
    /* A padded row: the key at its natural width, the value right-aligned in
     * the rest. */
    Rect key = { 0, 0, 0, 0 };
    Rect val = { 0, 0, 0, 0 };
    const int32_t cw = static_cast<int32_t>(row.w) - 2 * PAD_X;
    if ((cw > 0) && (row.h != 0)) {
        int32_t keyW = gfx_getTextWidth(FONT, KEYS[i].str());
        if (keyW > cw)
            keyW = cw;
        const int16_t x = static_cast<int16_t>(row.x + PAD_X);
        key = { x, row.y, static_cast<uint16_t>(keyW), row.h };
        val = { static_cast<int16_t>(x + keyW), row.y,
                static_cast<uint16_t>(cw - keyW), row.h };
    }

    char value[VALUE_SIZE];
    composeValue(i, value, sizeof(value));

    /* Clip to the row, striping every other row to keep the dense table
     * readable. */
    const bool clip = !row.empty();
    if (clip)
        d.pushClip(row);
    if ((i & 1u) != 0u)
        d.fillRect(row, Sem::Surface);
    paintText(d, key, TEXT_ALIGN_LEFT, Sem::OnSurfaceMuted, KEYS[i].str());
    paintText(d, val, TEXT_ALIGN_RIGHT, Sem::OnSurface, value);
    if (clip)
        d.popClip();
}

} // namespace ortxui
