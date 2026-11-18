/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_CHANNELSVIEW_HPP
#define ORTX_UI_CHANNELSVIEW_HPP

#include <cstdint>
#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Widgets.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/List.hpp"
#include "core/cps.h"

namespace ortxui
{

/**
 * The main-menu Channels browser: a scrollable list of the codeplug's channel
 * names (read on entry via cps_readChannel). ENTER loads the selected channel
 * into state.channel, requests an rtx resync, and unwinds to the home screen;
 * ESC goes back. When the codeplug has no channels a centred "No channels"
 * line is shown instead of the list.
 *
 * Rows are read on demand through a ListModel (only the visible window hits
 * the codeplug), so there is no per-channel storage regardless of codeplug
 * size — just one scratch name buffer.
 */
class ChannelsView : public View, public ListModel
{
public:
    void build();
    void onShow() override;
    NavIntent onEvent(const Event &e) override;

    Screen &screen() override
    {
        return screen_;
    }

    /* ListModel: the channel names, read lazily from the codeplug. */
    uint16_t rowCount() const override
    {
        return count_;
    }
    void rowAt(uint16_t index, ListItem &out) const override;

private:
    void reload();

    Screen screen_;
    Flex root_;
    TopBar topBar_;
    List list_;
    Label empty_;

    uint16_t count_ = 0;
    mutable char scratch_[CPS_STR_SIZE] = {};
};

} // namespace ortxui

#endif /* ORTX_UI_CHANNELSVIEW_HPP */
