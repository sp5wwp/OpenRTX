/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/InfoView.hpp"
#include "interfaces/platform.h"
#include "core/memory_profiling.h"
#include "hwconfig.h"

#include <cstdio>

namespace ortxui
{

void InfoView::build()
{
    static const UiText kKeys[RowCount] = {
        STR(batteryVoltage), STR(batteryCharge), STR(RSSI),
        STR(usedHeap),       STR(band),          STR(hwVersion),
    };

    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;
    const bool regular = (sizeClass() == SizeClass::Regular);
    const fontSize_t bodyFont = regular ? FONT_SIZE_8PT : FONT_SIZE_6PT;

    root_.setArea({ 0, 0, (uint16_t)W, (uint16_t)H });
    root_.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(info));
    root_.addChild(&bar);

    for (uint8_t i = 0; i < RowCount; i++) {
        rows_[i].setAxis(Axis::Row);
        rows_[i].setAlign(Align::Stretch);
        rows_[i].setPadding(4, 0);
        rows_[i].setGrow(1);
        /* Faint zebra striping keeps the dense table readable. */
        if ((i & 1u) != 0u)
            rows_[i].setBackground(Sem::Surface);

        keys_[i].setFont(bodyFont);
        keys_[i].setAlign(TEXT_ALIGN_LEFT);
        keys_[i].setColor(Sem::OnSurfaceMuted);
        /* Natural width: the key text length depends on the language. */
        keys_[i].setText(kKeys[i]);

        vals_[i].setFont(bodyFont);
        vals_[i].setAlign(TEXT_ALIGN_RIGHT);
        vals_[i].setColor(Sem::OnSurface);
        vals_[i].setText(valBufs_[i]);
        vals_[i].setGrow(1);

        rows_[i].addChild(&keys_[i]);
        rows_[i].addChild(&vals_[i]);
        root_.addChild(&rows_[i]);
    }

    /* Static values from hwInfo. */
    const hwInfo_t *hw = platform_getHwInfo();
    snprintf(valBufs_[RowBand], sizeof(valBufs_[RowBand]), "%s%s",
             hw->vhf_band ? "VHF " : "", hw->uhf_band ? "UHF" : "");
    snprintf(valBufs_[RowHwVer], sizeof(valBufs_[RowHwVer]), "%u",
             hw->hw_version);

    screen_.addChild(&root_);
    root_.onLayout();
    screen_.markAllDirty();
}

void InfoView::syncFromState(const state_t &s)
{
    View::syncFromState(s); /* refresh the shared top bar */

    if (s.v_bat != lastVbat_) {
        /* Integer volts + one decimal, rounding the mantissa to nearest. */
        const uint16_t volt = (s.v_bat + 50) / 1000;
        const uint16_t mv = ((s.v_bat - volt * 1000) + 50) / 100;
        snprintf(valBufs_[RowBattery], sizeof(valBufs_[RowBattery]), "%u.%uV",
                 volt, mv);
        vals_[RowBattery].invalidate();
        lastVbat_ = s.v_bat;
    }

    if (s.charge != lastCharge_) {
        snprintf(valBufs_[RowCharge], sizeof(valBufs_[RowCharge]), "%u%%",
                 s.charge);
        vals_[RowCharge].invalidate();
        lastCharge_ = s.charge;
    }

    if (s.rssi != lastRssi_) {
        snprintf(valBufs_[RowRssi], sizeof(valBufs_[RowRssi]), "%ld dBm",
                 (long)s.rssi);
        vals_[RowRssi].invalidate();
        lastRssi_ = s.rssi;
    }

    const unsigned used = getHeapSize() - getCurrentFreeHeap();
    if (used != lastHeapUsed_) {
        snprintf(valBufs_[RowHeap], sizeof(valBufs_[RowHeap]), "%u B", used);
        vals_[RowHeap].invalidate();
        lastHeapUsed_ = used;
    }
}

} // namespace ortxui
