/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/MenuView.hpp"
#include "core/Event.hpp"
#include "interfaces/keyboard.h"
#include "core/state.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"
#include "hwconfig.h"

namespace ortxui
{

void MenuView::build(const UiText &title, const UiText *items, uint16_t count)
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;
    const int16_t topH = (sizeClass() == SizeClass::Regular) ? 16 : 12;

    /* Shared top bar (title + clock + battery). */
    title_ = title;
    topBar_.init(title);
    topBar_.setArea({ 0, 0, (uint16_t)W, (uint16_t)topH });
    topBar_.onLayout();
    setTopBar(&topBar_);

    /* Scrollable list fills the space below the top bar (plain label rows). */
    if (count > kMaxRows)
        count = kMaxRows;
    for (uint16_t i = 0; i < count; i++)
        items_[i].label = items[i];

    list_.setArea({ 0, topH, (uint16_t)W, (uint16_t)(H - topH) });
    list_.setContentPad(1);
    list_.setItems(items_, count);
    list_.setFlag(FLAG_FOCUSABLE, true);

    screen_.addChild(&topBar_);
    screen_.addChild(&list_);
    screen_.focusFirst();
    screen_.markAllDirty();
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

    const ListItem *it = list_.selectedItem();
    if ((it != nullptr) && !it->label.empty())
        it->label.announce(vpqDefault);
    vp_play();
}

void MenuView::setRowTarget(uint16_t row, View *target)
{
    if (row < kMaxRows)
        rowTargets_[row] = target;
}

NavIntent MenuView::onEvent(const Event &e)
{
    if (e.kind == EvKind::Key) {
        if ((e.keys & KEY_ESC) != 0u)
            return NavIntent::pop();

        if ((e.keys & KEY_ENTER) != 0u) {
            const uint16_t sel = list_.selected();
            View *target = (sel < kMaxRows) ? rowTargets_[sel] : nullptr;
            if (target != nullptr)
                return NavIntent::push(target);
            return NavIntent::none(); /* leaf row: nothing to open yet */
        }
    }

    /* Encoder / UP / DOWN reach the focused List and move the selection. */
    screen_.dispatch(e);
    return NavIntent::none();
}

} // namespace ortxui
