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
    Background = 0, ///< Base app background
    Surface,        ///< Top bar and card surface, one step above background
    SurfaceHigh,    ///< Raised surface and meter track
    Separator,      ///< Thin divider line between rows
    OnSurface,      ///< Primary foreground text
    OnSurfaceMuted, ///< Secondary, unit and value text
    Primary,        ///< Interactive accent: selection fill, channel name
    OnPrimary,      ///< Text and icons drawn on a Primary fill
    Accent,         ///< Secondary brand accent: splash, headers
    OnAccent,       ///< Text and icons drawn on an Accent fill
    TxDanger,       ///< Transmit: TX meter, label and power
    RxSuccess,      ///< Receive and squelch open: RX meter and label
    Warning,        ///< Battery low and other cautions
    Mark,           ///< Checklist tick and alerts

    Count
};

} // namespace ortxui

#endif /* ORTX_UI_SEMANTICCOLOR_HPP */
