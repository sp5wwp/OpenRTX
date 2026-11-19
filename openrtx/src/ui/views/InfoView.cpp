/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/InfoView.hpp"
#include "hwconfig.h"

namespace ortxui
{

void InfoView::build()
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;

    root_.setArea({ 0, 0, (uint16_t)W, (uint16_t)H });
    root_.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(info));
    root_.addChild(&bar);

    for (uint8_t i = 0; i < InfoTable::ROW_COUNT; i++) {
        rows_[i].setGrow(1);
        /* Faint zebra striping keeps the dense table readable. */
        if ((i & 1u) != 0u)
            rows_[i].setBackground(Sem::Surface);
        root_.addChild(&rows_[i]);
    }

    screen_.addChild(&root_);
    root_.onLayout();

    Rect rows[InfoTable::ROW_COUNT];
    for (uint8_t i = 0; i < InfoTable::ROW_COUNT; i++)
        rows[i] = rows_[i].area();
    table_.place(rows);
    table_.setArea(
        { 0, rows[0].y, (uint16_t)W, static_cast<uint16_t>(H - rows[0].y) });
    screen_.addChild(&table_);
    screen_.markAllDirty();
}

void InfoView::syncFromState(const state_t &s)
{
    View::syncFromState(s); /* refresh the shared top bar */

    if (table_.sync(s))
        table_.invalidate();
}

} // namespace ortxui
