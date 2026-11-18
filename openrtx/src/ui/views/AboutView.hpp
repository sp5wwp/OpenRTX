/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_ABOUTVIEW_HPP
#define ORTX_UI_ABOUTVIEW_HPP

#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Widgets.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/List.hpp"

namespace ortxui
{

/**
 * The About screen: a title bar, the OpenRTX brand + firmware version, and a
 * scrolling (non-selectable) list of contributor credits. A leaf view — the
 * base View handles ESC (pop) and forwards the knob / up-down to the focused
 * credits list (reachable now that the Screen focus ring recurses into the
 * layout tree).
 */
class AboutView : public View
{
public:
    static constexpr uint16_t kMaxAuthors = 16;

    void build();

    Screen &screen() override
    {
        return screen_;
    }

private:
    Screen screen_;
    Flex root_;
    TopBar topBar_;
    Flex header_;
    Label brand_;
    Label version_;
    List authors_;
    ListItem authorItems_[kMaxAuthors] = {};
};

} // namespace ortxui

#endif /* ORTX_UI_ABOUTVIEW_HPP */
