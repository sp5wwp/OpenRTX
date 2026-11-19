/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/InfoTable.hpp"
#include "core/Layout.hpp"
#include "core/UiText.hpp"
#include "core/memory_profiling.h"
#include "interfaces/platform.h"
#include "render/DrawCtx.hpp"
#include "hwconfig.h"

#include <climits>
#include <cstdio>

namespace ortxui
{

/* Row keys, in row order. */
static const UiText KEYS[InfoTable::ROW_COUNT] = {
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
    row_(), key_(), value_(), heapUsed_(UINT32_MAX), rssi_(INT32_MIN),
    vbat_(UINT16_MAX), charge_(UINT8_MAX)
{
}

InfoTable::~InfoTable()
{
}

void InfoTable::place(const Rect *rows)
{
    for (uint8_t i = 0; i < ROW_COUNT; i++) {
        const Rect &r = rows[i];
        row_[i] = r;

        /* A padded row: the key at its natural width, the value growing into
         * the rest. Only the static values have text at this point. */
        char value[VALUE_SIZE] = "";
        if (i >= ROW_BAND)
            composeValue(i, value, sizeof(value));
        const int16_t keyW =
            static_cast<int16_t>(gfx_getTextWidth(FONT, KEYS[i].str()));
        const int16_t valueW =
            static_cast<int16_t>(gfx_getTextWidth(FONT, value));
        const int cw = static_cast<int>(r.w) - 2 * PAD_X;
        if ((cw <= 0) || (r.h == 0)) {
            key_[i] = { 0, 0, 0, 0 };
            value_[i] = { 0, 0, 0, 0 };
            continue;
        }

        int free = cw - keyW - valueW;
        if (free < 0)
            free = 0;
        const int16_t x = static_cast<int16_t>(r.x + PAD_X);
        key_[i] = { x, r.y, static_cast<uint16_t>(keyW), r.h };
        value_[i] = { static_cast<int16_t>(x + keyW), r.y,
                      static_cast<uint16_t>(valueW + free), r.h };
    }
}

void InfoTable::composeValue(uint8_t row, char *out, size_t n) const
{
    const hwInfo_t *hw = platform_getHwInfo();

    switch (row) {
        case ROW_BATTERY: {
            /* Integer volts + one decimal, rounding the mantissa to nearest. */
            const uint16_t volt = (vbat_ + 50) / 1000;
            const uint16_t mv = ((vbat_ - volt * 1000) + 50) / 100;
            snprintf(out, n, "%u.%uV", volt, mv);
            break;
        }
        case ROW_CHARGE:
            snprintf(out, n, "%u%%", charge_);
            break;
        case ROW_RSSI:
            snprintf(out, n, "%ld dBm", (long)rssi_);
            break;
        case ROW_HEAP:
            snprintf(out, n, "%u B", (unsigned)heapUsed_);
            break;
        case ROW_BAND:
            snprintf(out, n, "%s%s", hw->vhf_band ? "VHF " : "",
                     hw->uhf_band ? "UHF" : "");
            break;
        default:
            snprintf(out, n, "%u", hw->hw_version);
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
    for (uint8_t i = 0; i < ROW_COUNT; i++) {
        char value[VALUE_SIZE];
        composeValue(i, value, sizeof(value));

        /* Clip to the row as its container would. */
        const bool clip = !row_[i].empty();
        if (clip)
            d.pushClip(row_[i]);
        paintText(d, key_[i], TEXT_ALIGN_LEFT, Sem::OnSurfaceMuted,
                  KEYS[i].str());
        paintText(d, value_[i], TEXT_ALIGN_RIGHT, Sem::OnSurface, value);
        if (clip)
            d.popClip();
    }
}

} // namespace ortxui
