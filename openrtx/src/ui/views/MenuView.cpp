/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/MenuView.hpp"
#include "core/Event.hpp"
#include "core/utils.h"
#include "interfaces/keyboard.h"
#include "core/state.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"
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
    if (id() == ViewId::SettingsMenu) {
        title_ = STR(settings);
        rows_ = SETTINGS_MENU;
        count_ = ARRAY_SIZE(SETTINGS_MENU);
    } else {
        title_ = STR(menu);
        rows_ = MAIN_MENU;
        count_ = ARRAY_SIZE(MAIN_MENU);
    }

    root_.setArea({ 0, 0, static_cast<uint16_t>(CONFIG_SCREEN_WIDTH),
                    static_cast<uint16_t>(CONFIG_SCREEN_HEIGHT) });
    root_.setAxis(Axis::Column);

    /* The list of plain label rows fills the space below the top bar. */
    TopBar &bar = useTopBar(title_);
    list_.setContentPad(1);
    list_.setModel(this);
    list_.setFlag(FLAG_FOCUSABLE, true);
    list_.setGrow(1);

    root_.addChild(&bar);
    root_.addChild(&list_);
    screen_.addChild(&root_);
    root_.onLayout();
    screen_.focusFirst();
    screen_.markAllDirty();
}

void MenuView::rowAt(uint16_t index, ListItem &out) const
{
    out.label = rows_[index].label;
}

void MenuView::announce()
{
    /* On entry speak the menu title then the current row (one utterance, so no
     * intermediate flush — List::announceSelection flushes and is for nav). */
    if (state.settings.vpLevel < vpLow)
        return;

    const enum vpQueueFlags flags = vp_getVoiceLevelQueueFlags();
    vp_flush();
    title_.announce(flags);

    const UiText &label = rows_[list_.selected()].label;
    if (!label.empty())
        label.announce(vpqDefault);
    vp_play();
}

NavIntent MenuView::onEvent(const Event &e)
{
    if (e.kind == EvKind::Key) {
        if ((e.keys & KEY_ESC) != 0u)
            return NavIntent::pop();

        if ((e.keys & KEY_ENTER) != 0u)
            return NavIntent::push(rows_[list_.selected()].target);
    }

    /* Encoder / UP / DOWN reach the focused List and move the selection. */
    screen_.dispatch(e);
    return NavIntent::none();
}

} // namespace ortxui
