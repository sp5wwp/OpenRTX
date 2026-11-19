/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/ContactsView.hpp"
#include "core/Event.hpp"
#include "interfaces/keyboard.h"
#include "hwconfig.h"

extern "C" {
#include "interfaces/cps_io.h"
}

#include <cstring>

namespace ortxui
{

void ContactsView::build()
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;
    const bool regular = (sizeClass() == SizeClass::Regular);

    root_.setArea({ 0, 0, (uint16_t)W, (uint16_t)H });
    root_.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(contacts));

    list_.setContentPad(1);
    list_.setFlag(FLAG_FOCUSABLE, true);
    list_.setGrow(1);

    empty_.setFont(regular ? FONT_SIZE_10PT : FONT_SIZE_8PT);
    empty_.setAlign(TEXT_ALIGN_CENTER);
    empty_.setColor(Sem::OnSurfaceMuted);
    empty_.setText(STR(noContacts));
    empty_.setGrow(1);

    root_.addChild(&bar);
    root_.addChild(&list_);
    root_.addChild(&empty_);
    screen_.addChild(&root_);

    reload();
}

void ContactsView::rowAt(uint16_t index, ListItem &out) const
{
    contact_t ct;
    if (cps_readContact(&ct, index) == 0) {
        strncpy(scratch_, ct.name, CPS_STR_SIZE - 1);
        scratch_[CPS_STR_SIZE - 1] = '\0';
    } else {
        scratch_[0] = '\0';
    }
    out.label = scratch_;
    out.value = nullptr;
    out.checkbox = false;
}

void ContactsView::reload()
{
    contact_t ct;
    count_ = 0;
    while ((count_ < 0xFFFEu) && (cps_readContact(&ct, count_) == 0))
        count_++;

    const bool empty = (count_ == 0);
    list_.setModel(this);
    list_.setSelected(0);
    list_.setFlag(FLAG_HIDDEN, empty);
    empty_.setFlag(FLAG_HIDDEN, !empty);

    root_.onLayout();
    if (!empty)
        screen_.focusFirst();
    screen_.markAllDirty();
}

void ContactsView::onShow()
{
    /* Reload on open — the codeplug may not have been read when build() ran. */
    reload();
}

NavIntent ContactsView::onEvent(const Event &e)
{
    if ((e.kind == EvKind::Key) && ((e.keys & KEY_ESC) != 0u))
        return NavIntent::pop();

    /* Read-only viewer: ENTER has no action (matches the classic UI); the List
     * still scrolls on UP/DOWN/encoder via the Screen focus ring. */
    screen_.dispatch(e);
    return NavIntent::none();
}

} // namespace ortxui
