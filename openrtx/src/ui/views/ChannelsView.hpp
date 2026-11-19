/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_CHANNELSVIEW_HPP
#define ORTX_UI_CHANNELSVIEW_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>
#include "views/CodeplugView.hpp"

namespace ortxui
{

/**
 * The main-menu channel browser. ENTER loads the selected channel, leaves
 * any bank and goes back to the home screen in memory mode.
 */
class ChannelsView : public CodeplugView
{
public:
    /** Constructor. */
    ChannelsView();

    /** Destructor. */
    ~ChannelsView() = default;

private:
    /** Read a channel's name. */
    int readName(uint16_t index, char *name) const override;

    /** Load the channel on the row. */
    NavIntent pick(uint16_t row) override;
};

} // namespace ortxui

#endif /* ORTX_UI_CHANNELSVIEW_HPP */
