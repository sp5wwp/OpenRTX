/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_ABOUTVIEW_HPP
#define ORTX_UI_ABOUTVIEW_HPP

#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Label.hpp"
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
    void build() override;

    Screen &screen() override
    {
        return screen_;
    }

    /** The credits list, whose scroll UiMemory keeps. */
    List *list() override
    {
        return &authors_;
    }

private:
    Screen screen_;
    Flex root_;
    Flex header_;
    Label brand_;
    Label version_;
    List authors_;
};

} // namespace ortxui

#endif /* ORTX_UI_ABOUTVIEW_HPP */
