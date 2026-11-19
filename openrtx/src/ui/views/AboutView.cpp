/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/AboutView.hpp"
#include "core/utils.h"
#include "core/voicePromptUtils.h"
#include "hwconfig.h"

namespace ortxui
{

/* Contributor credits, as list rows the list borrows from flash. */
static constexpr ListItem AUTHORS[] = {
    { STR(Niccolo), UiText() },
    { STR(Silvano), UiText() },
    { STR(Federico), UiText() },
    { STR(Fred), UiText() },
    { STR(Joseph), UiText() },
    { UiText("Morgan ON4MOD"), UiText() },
    { UiText("Marco DM4RCO"), UiText() },
};

void AboutView::build()
{
    const bool regular = (sizeClass() == SizeClass::Regular);

    root.setArea(screenArea());
    root.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(about));

    /* Brand + version block. */
    header.setAxis(Axis::Column);
    header.setAlign(Align::Stretch);
    header.setJustify(Justify::Center);
    header.setGap(1);
    header.setBasis(regular ? 30 : 24);

    brand.setFont(FONT_SIZE_8PT);
    brand.setAlign(TEXT_ALIGN_CENTER);
    brand.setColor(Sem::Accent); /* OpenRTX gold, kept as a secondary accent */
    brand.setText("OpenRTX");
    brand.setBasis(gfx_getFontHeight(FONT_SIZE_8PT));

    version.setFont(FONT_SIZE_6PT);
    version.setAlign(TEXT_ALIGN_CENTER);
    version.setColor(Sem::OnSurfaceMuted);
    version.setText(GIT_VERSION);
    version.setBasis(gfx_getFontHeight(FONT_SIZE_6PT));

    header.addChild(&brand);
    header.addChild(&version);

    /* Scrolling, non-selectable credits list fills the rest. */
    authors.setItems(AUTHORS, ARRAY_SIZE(AUTHORS));
    authors.setContentPad(1);
    authors.setSelectable(false);
    authors.setGrow(1);
    authors.setFlag(FLAG_FOCUSABLE, true);

    root.addChild(&bar);
    root.addChild(&header);
    root.addChild(&authors);
    tree.addChild(&root);
    root.onLayout();
    tree.focusFirst(); /* recurses into the layout tree to reach the list */

    tree.markAllDirty();
}

void AboutView::announce()
{
    if (state.settings.vpLevel >= vpLow)
        vp_announceAboutScreen();
}

} // namespace ortxui
