/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "base/View.hpp"
#include "base/Event.hpp"
#include "widgets/List.hpp"
#include "widgets/TopBar.hpp"
#include "interfaces/keyboard.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"
#include "hwconfig.h"

namespace ortxui
{

/* One top bar serves every screen, since only one view exists at a time. */
static TopBar sharedTopBar;

Rect View::screenArea()
{
    return { 0, 0, CONFIG_SCREEN_WIDTH, CONFIG_SCREEN_HEIGHT };
}

TopBar &View::topBar()
{
    return sharedTopBar;
}

TopBar &View::useTopBar(const UiText &title)
{
    sharedTopBar.reset(title);
    hasTopBar_ = true;
    return sharedTopBar;
}

void View::syncFromState(const state_t &s)
{
    if (hasTopBar_)
        sharedTopBar.update(s);
}

void View::announce()
{
    const UiText title = hasTopBar_ ? sharedTopBar.title() : UiText();
    if (const List *l = list())
        l->announceSelection(title);
    else
        vpSay(title);
}

void View::vpSay(const UiText &text)
{
    if ((state.settings.vpLevel < vpLow) || text.empty())
        return;

    vp_flush();
    text.announce(vp_getVoiceLevelQueueFlags());
    vp_play();
}

NavIntent View::onEvent(const Event &e)
{
    if ((e.kind == EvKind::Key) && ((e.keys & KEY_ESC) != 0u))
        return NavIntent::pop();

    screen().dispatch(e);
    return NavIntent::none();
}

} // namespace ortxui
