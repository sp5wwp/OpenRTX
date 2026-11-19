/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/AboutView.hpp"
#include "hwconfig.h"

namespace ortxui
{

/* Contributor credits (borrowed static table; mirrors the classic UI). */
static const char *const kAuthors[] = {
    "Niccolo' IU2KIN", "Silvano IU2KWO", "Federico IU2NUO", "Fred IU2NRO",
    "Joseph VK7JS",    "Morgan ON4MOD",  "Marco DM4RCO",
};
static constexpr uint16_t kAuthorCount = sizeof(kAuthors) / sizeof(kAuthors[0]);

void AboutView::build()
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;
    const bool regular = (sizeClass() == SizeClass::Regular);

    root_.setArea({ 0, 0, (uint16_t)W, (uint16_t)H });
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
    uint16_t n = kAuthorCount;
    if (n > kMaxAuthors)
        n = kMaxAuthors;
    for (uint16_t i = 0; i < n; i++)
        authorItems_[i].label = kAuthors[i];

    authors_.setItems(authorItems_, n);
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

} // namespace ortxui
