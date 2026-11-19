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

    root_.setArea({ 0, 0, static_cast<uint16_t>(CONFIG_SCREEN_WIDTH),
                    static_cast<uint16_t>(CONFIG_SCREEN_HEIGHT) });
    root_.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(about));

    /* Brand + version block. */
    header_.setAxis(Axis::Column);
    header_.setAlign(Align::Stretch);
    header_.setJustify(Justify::Center);
    header_.setGap(1);
    header_.setBasis(regular ? 30 : 24);

    brand_.setFont(FONT_SIZE_8PT);
    brand_.setAlign(TEXT_ALIGN_CENTER);
    brand_.setColor(Sem::Accent); /* OpenRTX gold, kept as a secondary accent */
    brand_.setText("OpenRTX");
    brand_.setBasis(gfx_getFontHeight(FONT_SIZE_8PT));

    version_.setFont(FONT_SIZE_6PT);
    version_.setAlign(TEXT_ALIGN_CENTER);
    version_.setColor(Sem::OnSurfaceMuted);
    version_.setText(GIT_VERSION);
    version_.setBasis(gfx_getFontHeight(FONT_SIZE_6PT));

    header_.addChild(&brand_);
    header_.addChild(&version_);

    /* Scrolling, non-selectable credits list fills the rest. */
    authors_.setItems(AUTHORS, ARRAY_SIZE(AUTHORS));
    authors_.setContentPad(1);
    authors_.setSelectable(false);
    authors_.setGrow(1);
    authors_.setFlag(FLAG_FOCUSABLE, true);

    root_.addChild(&bar);
    root_.addChild(&header_);
    root_.addChild(&authors_);
    screen_.addChild(&root_);
    root_.onLayout();
    screen_.focusFirst(); /* recurses into the layout tree to reach the list */

    screen_.markAllDirty();
}

void AboutView::announce()
{
    if (state.settings.vpLevel >= vpLow)
        vp_announceAboutScreen();
}

} // namespace ortxui
