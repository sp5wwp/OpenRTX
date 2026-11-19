/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/ChannelsView.hpp"
#include "core/Event.hpp"
#include "core/state.h"
#include "interfaces/keyboard.h"
#include "hwconfig.h"

extern "C" {
#include "interfaces/cps_io.h"
}

#include <cstring>

namespace ortxui
{

void ChannelsView::build()
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;
    const bool regular = (sizeClass() == SizeClass::Regular);

    root_.setArea({ 0, 0, (uint16_t)W, (uint16_t)H });
    root_.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(channels));

    list_.setContentPad(1);
    list_.setFlag(FLAG_FOCUSABLE, true);
    list_.setGrow(1);

    empty_.setFont(regular ? FONT_SIZE_10PT : FONT_SIZE_8PT);
    empty_.setAlign(TEXT_ALIGN_CENTER);
    empty_.setColor(Sem::OnSurfaceMuted);
    empty_.setText(STR(noChannels));
    empty_.setGrow(1);

    root_.addChild(&bar);
    root_.addChild(&list_);
    root_.addChild(&empty_);
    screen_.addChild(&root_);

    reload();
}

void ChannelsView::rowAt(uint16_t index, ListItem &out) const
{
    channel_t ch;
    if (cps_readChannel(&ch, index) == 0) {
        strncpy(scratch_, ch.name, CPS_STR_SIZE - 1);
        scratch_[CPS_STR_SIZE - 1] = '\0';
    } else {
        scratch_[0] = '\0';
    }
    out.label = scratch_;
    out.value = nullptr;
    out.checkbox = false;
}

void ChannelsView::reload()
{
    /* Count channels by probing (the codeplug exposes no count getter); the
     * List then reads only the visible rows via the model. */
    channel_t ch;
    count_ = 0;
    while ((count_ < 0xFFFEu) && (cps_readChannel(&ch, count_) == 0))
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

void ChannelsView::onShow()
{
    /* Reload each time the browser is opened (the codeplug is read lazily and
     * may not have been open when build() ran). */
    reload();
}

NavIntent ChannelsView::onEvent(const Event &e)
{
    if (e.kind == EvKind::Key) {
        if ((e.keys & KEY_ESC) != 0u)
            return NavIntent::pop();

        if (((e.keys & KEY_ENTER) != 0u) && (count_ > 0)) {
            const uint16_t sel = list_.selected();
            channel_t ch;
            if (cps_readChannel(&ch, sel) == 0) {
                /* Preserve the current VFO so an ESC back from memory mode
                 * restores it (see VfoView's VFO/MEM toggle). */
                if (state.tuner_mode == VFO)
                    state.vfo_channel = state.channel;
                state.channel = ch;
                state.channel_index = sel;
                state.tuner_mode = CH; /* memory mode: VFO shows name + index */
                requestSyncRtx();
                return NavIntent::popToRoot();
            }
            return NavIntent::none();
        }
    }

    screen_.dispatch(e);
    return NavIntent::none();
}

} // namespace ortxui
