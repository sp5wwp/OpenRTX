/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/ChannelsView.hpp"
#include "core/state.h"

extern "C" {
#include "interfaces/cps_io.h"
}

namespace ortxui
{

ChannelsView::ChannelsView() :
    CodeplugView(STR(channels), STR(noChannels), UiText())
{
}

int ChannelsView::readName(uint16_t index, char *name) const
{
    channel_t ch;
    const int ret = cps_readChannel(&ch, index);
    if (ret == 0)
        copyName(name, ch.name);

    return ret;
}

NavIntent ChannelsView::pick(uint16_t row)
{
    channel_t ch;
    if (cps_readChannel(&ch, row) != 0)
        return NavIntent::none();

    /* Keep the VFO, so leaving memory mode goes back to it. */
    if (state.tuner_mode == VFO)
        state.vfo_channel = state.channel;
    state.channel = ch;
    state.channel_index = row;
    state.tuner_mode = CH;
    /* The index is global, so stepping must not map it through a bank. */
    state.bank_enabled = false;
    requestSyncRtx();
    return NavIntent::popToRoot();
}

} // namespace ortxui
