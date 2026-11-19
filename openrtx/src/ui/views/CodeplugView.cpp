/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/CodeplugView.hpp"
#include "base/Event.hpp"
#include "interfaces/keyboard.h"
#include "widgets/TopBar.hpp"
#include "hwconfig.h"

#include <cstring>

namespace ortxui
{

/** Most entries counted, below the largest codeplug index. */
static constexpr uint16_t MAX_ENTRIES = 0xFFFE;

CodeplugView::CodeplugView(const UiText &title, const UiText &none,
                           const UiText &first) :
    titleText(title), noneText(none), firstText(first)
{
}

void CodeplugView::build()
{
    const bool regular = (sizeClass() == SizeClass::Regular);

    /* The codeplug keeps no count, so probe it, once per opening. */
    while ((entries < MAX_ENTRIES) && (readName(entries, nameBuf) == 0))
        entries++;

    const bool empty = (rowCount() == 0);

    root.setArea({ 0, 0, CONFIG_SCREEN_WIDTH, CONFIG_SCREEN_HEIGHT });
    root.setAxis(Axis::Column);

    TopBar &bar = useTopBar(titleText);

    listBox.setContentPad(1);
    listBox.setFlag(FlagFocusable, true);
    listBox.setFlag(FlagHidden, empty);
    listBox.setGrow(1);
    listBox.setModel(this);

    emptyLabel.setFont(regular ? FONT_SIZE_10PT : FONT_SIZE_8PT);
    emptyLabel.setAlign(TEXT_ALIGN_CENTER);
    emptyLabel.setColor(Sem::OnSurfaceMuted);
    emptyLabel.setText(noneText);
    emptyLabel.setFlag(FlagHidden, !empty);
    emptyLabel.setGrow(1);

    root.addChild(&bar);
    root.addChild(&listBox);
    root.addChild(&emptyLabel);
    tree.addChild(&root);
    root.onLayout();
    tree.focusFirst();
}

uint16_t CodeplugView::rowCount() const
{
    return static_cast<uint16_t>(entries + (firstText.isSet() ? 1 : 0));
}

void CodeplugView::rowAt(uint16_t index, ListItem &out) const
{
    if (firstText.isSet()) {
        if (index == 0) {
            out.label = firstText;
            return;
        }
        index--;
    }

    if (readName(index, nameBuf) != 0)
        nameBuf[0] = '\0';
    out.label = nameBuf;
}

void CodeplugView::copyName(char *dst, const char *src)
{
    strncpy(dst, src, CPS_STR_SIZE - 1);
    dst[CPS_STR_SIZE - 1] = '\0';
}

NavIntent CodeplugView::pick(uint16_t row)
{
    (void)row;
    return NavIntent::none();
}

NavIntent CodeplugView::onEvent(const Event &e)
{
    if ((e.kind == EvKind::Key) && ((e.keys & KEY_ENTER) != 0u)
        && (rowCount() > 0))
        return pick(listBox.selected());

    return View::onEvent(e);
}

} // namespace ortxui
