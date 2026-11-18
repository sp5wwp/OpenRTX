/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_SEMANTICCOLOR_HPP
#define ORTX_UI_SEMANTICCOLOR_HPP

#include <cstdint>

namespace ortxui
{

/**
 * Semantic colour roles.
 *
 * Widgets reference roles, never raw colours. A Theme maps each role to a
 * concrete framebuffer colour for the active target, and collapses them to
 * ink/paper on 1bpp monochrome targets. This is what lets one screen render
 * in full colour on the 160x128 RGB565 panels and legibly in black & white on
 * the 128x64 mono panels with no per-widget branching.
 */
enum class Sem : uint8_t {
    Background = 0, //< Base app background (true black)
    Surface,        //< Top bar / card surface, one step above background
    SurfaceHigh,    //< Raised surface / meter track
    Separator,      //< Thin divider line between rows
    OnSurface,      //< Primary foreground text (white)
    OnSurfaceMuted, //< Secondary / unit / value text (grey)
    Primary,        //< Interactive accent (blue): selection fill, channel name
    OnPrimary,      //< Text/icon drawn on top of a Primary (blue) fill (black)
    Accent,         //< Secondary brand accent (gold): splash, headers, focus
    OnAccent,       //< Text/icon drawn on top of an Accent (gold) fill (black)
    TxDanger,       //< Transmit (orange): TX meter dots + label + power
    RxSuccess,      //< Receive / squelch-open (green): RX meter dots + label
    Warning,        //< Battery-low / caution (orange)
    Mark,           //< Checklist tick / alert (red-orange)
    ModeM17,        //< (transitional) M17 mode badge colour
    ModeM17Accent,  //< (transitional) M17 logo secondary grey
    ModeFM,         //< (transitional) FM mode badge
    ModeDMR,        //< (transitional) DMR mode badge (reuses RxSuccess green)
    FocusRing,      //< Focus highlight (reuses Accent gold)

    Count
};

} // namespace ortxui

#endif /* ORTX_UI_SEMANTICCOLOR_HPP */
