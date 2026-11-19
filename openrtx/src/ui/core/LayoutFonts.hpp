/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_LAYOUTFONTS_HPP
#define ORTX_UI_LAYOUTFONTS_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

namespace ortxui
{

/**
 * Selects the smooth font metrics for as long as it lives. Screens are laid
 * out with them, as when every view was built at start-up before the crisp
 * text setting applied, so a widget that redoes a layout when drawn takes
 * them too and lands where the build put it.
 */
class LayoutFonts
{
public:
    /** Constructor: select the smooth metrics. */
    LayoutFonts();

    /** Destructor: select the fonts of the text setting again. */
    ~LayoutFonts();

    LayoutFonts(const LayoutFonts &) = delete;
    LayoutFonts &operator=(const LayoutFonts &) = delete;
};

} // namespace ortxui

#endif /* ORTX_UI_LAYOUTFONTS_HPP */
