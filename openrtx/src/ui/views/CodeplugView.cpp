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
    title_(title), none_(none), first_(first)
{
}

void CodeplugView::build()
{
    const bool regular = (sizeClass() == SizeClass::Regular);

    /* The codeplug keeps no count, so probe it, once per opening. */
    while ((entries_ < MAX_ENTRIES) && (readName(entries_, name_) == 0))
        entries_++;

    const bool empty = (rowCount() == 0);

    root_.setArea({ 0, 0, CONFIG_SCREEN_WIDTH, CONFIG_SCREEN_HEIGHT });
    root_.setAxis(Axis::Column);

    TopBar &bar = useTopBar(title_);

    list_.setContentPad(1);
    list_.setFlag(FLAG_FOCUSABLE, true);
    list_.setFlag(FLAG_HIDDEN, empty);
    list_.setGrow(1);
    list_.setModel(this);

    empty_.setFont(regular ? FONT_SIZE_10PT : FONT_SIZE_8PT);
    empty_.setAlign(TEXT_ALIGN_CENTER);
    empty_.setColor(Sem::OnSurfaceMuted);
    empty_.setText(none_);
    empty_.setFlag(FLAG_HIDDEN, !empty);
    empty_.setGrow(1);

    root_.addChild(&bar);
    root_.addChild(&list_);
    root_.addChild(&empty_);
    screen_.addChild(&root_);
    root_.onLayout();
    screen_.focusFirst();
}

uint16_t CodeplugView::rowCount() const
{
    return static_cast<uint16_t>(entries_ + (first_.isSet() ? 1 : 0));
}

void CodeplugView::rowAt(uint16_t index, ListItem &out) const
{
    if (first_.isSet()) {
        if (index == 0) {
            out.label = first_;
            return;
        }
        index--;
    }

    if (readName(index, name_) != 0)
        name_[0] = '\0';
    out.label = name_;
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
        return pick(list_.selected());

    return View::onEvent(e);
}

} // namespace ortxui
