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

    /* The table fills the rest, its rows sharing the height. */
    table_.setGrow(1);
    root_.addChild(&table_);

    screen_.addChild(&root_);
    root_.onLayout();
    screen_.markAllDirty();
}

void InfoView::syncFromState(const state_t &s)
{
    View::syncFromState(s); /* refresh the shared top bar */

    if (table_.sync(s))
        table_.invalidate();
}

} // namespace ortxui
