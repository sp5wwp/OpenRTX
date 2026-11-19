/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_INFOVIEW_HPP
#define ORTX_UI_INFOVIEW_HPP

#include <cstdint>
#include "core/View.hpp"
#include "views/InfoTable.hpp"
#include "layout/Flex.hpp"
#include "widgets/Label.hpp"
#include "widgets/TopBar.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The Info screen: a title bar over a table of key/value rows (firmware
 * battery, RSSI, heap, band, hardware version). Each row is a Flex row
 * [key | value] and the rows share the space equally (grow), so the table fills
 * the screen without hand-placed coordinates or scrolling. A leaf view: ESC
 * pops back. syncFromState() refreshes the live values (battery/charge/RSSI/
 * heap) change-gated; the rest are filled once at build. (The firmware version
 * is shown on the About screen rather than duplicated here.)
 */
class InfoView : public View
{
public:
    void build() override;
    void syncFromState(const state_t &s) override;

    Screen &screen() override
    {
        return screen_;
    }

private:
    Screen screen_;
    Flex root_;
    InfoTable table_; //< zebra-striped rows of keys and values
};

} // namespace ortxui

#endif /* ORTX_UI_INFOVIEW_HPP */
