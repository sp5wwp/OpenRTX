/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_ABOUTVIEW_HPP
#define ORTX_UI_ABOUTVIEW_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include "views/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Label.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/List.hpp"

namespace ortxui
{

/**
 * The About screen: the OpenRTX name and firmware version over a list of
 * contributor credits that the knob or UP/DOWN scroll.
 */
class AboutView : public View
{
public:
    /** Constructor. */
    AboutView() = default;

    /** Destructor. */
    ~AboutView() = default;

    /** Build the header over the credits. */
    void build() override;

    /** Speak the name and the credits. */
    void announce() override;

    /** The view's widget-tree root. */
    Screen &screen() override
    {
        return tree;
    }

    /** The credits list, whose scroll UiMemory keeps. */
    List *list() override
    {
        return &authors;
    }

private:
    Screen tree;   ///< Widget tree root
    Flex root;     ///< Top bar, header and credits
    Flex header;   ///< Name over version
    Label brand;   ///< "OpenRTX"
    Label version; ///< Firmware version
    List authors;  ///< Credits
};

} // namespace ortxui

#endif /* ORTX_UI_ABOUTVIEW_HPP */
