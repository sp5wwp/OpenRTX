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
    root.setArea({ 0, 0, static_cast<uint16_t>(CONFIG_SCREEN_WIDTH),
                   static_cast<uint16_t>(CONFIG_SCREEN_HEIGHT) });
    root.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(info));
    root.addChild(&bar);

    /* The table fills the rest, its rows sharing the height. */
    table.setGrow(1);
    root.addChild(&table);

    tree.addChild(&root);
    root.onLayout();
    tree.markAllDirty();
}

void InfoView::syncFromState(const state_t &s)
{
    View::syncFromState(s); /* refresh the shared top bar */

    if (table.sync(s))
        table.invalidate();
}

} // namespace ortxui
