/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/View.hpp"
#include "core/Event.hpp"
#include "widgets/TopBar.hpp"
#include "interfaces/keyboard.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"

namespace ortxui
{

/* One top bar serves every screen, since only one view exists at a time. */
static TopBar sharedTopBar;

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
    /* Default: speak the screen's top-bar title on entry (titled screens get
     * this for free; List navigation is announced by the List). */
    if (hasTopBar_)
        vpSay(sharedTopBar.title());
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
