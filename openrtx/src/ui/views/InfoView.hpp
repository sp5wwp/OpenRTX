/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_INFOVIEW_HPP
#define ORTX_UI_INFOVIEW_HPP

#include <cstdint>
#include "base/View.hpp"
#include "views/InfoTable.hpp"
#include "layout/Flex.hpp"
#include "widgets/TopBar.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The Info screen: battery voltage and charge, RSSI, heap use, bands and
 * hardware version, one row each, under the title bar. The rows share the
 * screen's height, so it never scrolls; the firmware version is on About.
 */
class InfoView : public View
{
public:
    /** Constructor. */
    InfoView() = default;

    /** Destructor. */
    ~InfoView() = default;

    /** Build the title bar over the table. */
    void build() override;

    /** Repaint the table when a reading it shows changed. */
    void syncFromState(const state_t &s) override;

    /** The view's widget-tree root. */
    Screen &screen() override
    {
        return screen_;
    }

private:
    Screen screen_;   ///< Widget tree root
    Flex root_;       ///< Top bar over the table
    InfoTable table_; ///< Striped rows of keys and values
};

} // namespace ortxui

#endif /* ORTX_UI_INFOVIEW_HPP */
