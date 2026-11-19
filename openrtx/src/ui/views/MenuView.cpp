/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/MenuView.hpp"
#include "base/Event.hpp"
#include "core/utils.h"
#include "interfaces/keyboard.h"
#include "hwconfig.h"

namespace ortxui
{

/* Menu tables; a screen the target lacks has no row. */
static constexpr MenuRow MAIN_MENU[] = {
    { STR(banks), ViewId::Banks },
    { STR(channels), ViewId::Channels },
    { STR(contacts), ViewId::Contacts },
#ifdef CONFIG_GPS
    { STR(gps), ViewId::Gps },
#endif
#ifdef CONFIG_MESSAGES
    { STR(messages), ViewId::Messages },
#endif
    { STR(settings), ViewId::SettingsMenu },
    { STR(info), ViewId::Info },
    { STR(about), ViewId::About },
};

static constexpr MenuRow SETTINGS_MENU[] = {
    { STR(display), ViewId::Display },
#ifdef CONFIG_RTC
    { STR(timeAndDate), ViewId::TimeDate },
#endif
#ifdef CONFIG_GPS
    { STR(gps), ViewId::GpsSettings },
#endif
    { STR(radio), ViewId::Radio },
#ifdef CONFIG_M17
    { STR(m17), ViewId::M17 },
#endif
    { STR(fm), ViewId::Fm },
    { STR(accessibility), ViewId::Accessibility },
    { STR(defaultSettings), ViewId::Defaults },
};

void MenuView::build()
{
    const bool settings = (id() == ViewId::SettingsMenu);
    rows = settings ? SETTINGS_MENU : MAIN_MENU;
    count = settings ? ARRAY_SIZE(SETTINGS_MENU) : ARRAY_SIZE(MAIN_MENU);

    root.setArea(screenArea());
    root.setAxis(Axis::Column);

    /* The list of plain label rows fills the space below the top bar. */
    TopBar &bar = useTopBar(settings ? STR(settings) : STR(menu));
    listBox.setContentPad(1);
    listBox.setModel(this);
    listBox.setFlag(FlagFocusable, true);
    listBox.setGrow(1);

    root.addChild(&bar);
    root.addChild(&listBox);
    tree.addChild(&root);
    root.onLayout();
    tree.focusFirst();
    tree.markAllDirty();
}

void MenuView::rowAt(uint16_t index, ListItem &out) const
{
    out.label = rows[index].label;
}

NavIntent MenuView::onEvent(const Event &e)
{
    if (e.kind == EvKind::Key) {
        if ((e.keys & KEY_ESC) != 0u)
            return NavIntent::pop();

        if ((e.keys & KEY_ENTER) != 0u)
            return NavIntent::push(rows[listBox.selected()].target);
    }

    /* Encoder / UP / DOWN reach the focused List and move the selection. */
    tree.dispatch(e);
    return NavIntent::none();
}

} // namespace ortxui
