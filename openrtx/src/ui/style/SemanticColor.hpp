/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_SEMANTICCOLOR_HPP
#define ORTX_UI_SEMANTICCOLOR_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>

namespace ortxui
{

/**
 * Semantic colour roles. Widgets draw with roles, never raw colours, so one
 * screen renders on colour and 1bpp panels alike; the theme maps each role.
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

    Count           ///< Number of roles
};

} // namespace ortxui

#endif /* ORTX_UI_SEMANTICCOLOR_HPP */
