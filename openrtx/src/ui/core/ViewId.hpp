/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_VIEWID_HPP
#define ORTX_UI_VIEWID_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>

namespace ortxui
{

/**
 * Identifies a screen. Only one View exists at a time, so navigation names the
 * screen to build rather than pointing at a live object. Screens of features a
 * target lacks keep their id but cannot be built.
 */
enum class ViewId : uint8_t {
    Vfo,            ///< Home screen
    MainMenu,       ///< Main menu
    SettingsMenu,   ///< Settings menu
    Macro,          ///< Macro menu, shown while MONI is held
    Info,           ///< Info
    About,          ///< About
    Display,        ///< Settings > Display
    TimeDate,       ///< Settings > Time & Date
    Accessibility,  ///< Settings > Accessibility
    Fm,             ///< Settings > FM
    Radio,          ///< Settings > Radio
    M17,            ///< Settings > M17
    Defaults,       ///< Settings > Default Settings
    Channels,       ///< Channel browser
    Contacts,       ///< Contact browser
    Banks,          ///< Bank browser
    TextInput,      ///< Modal text editor
    Picker,         ///< Character picker of the text editor
    Gps,            ///< GPS status
    GpsSettings,    ///< Settings > GPS
    Messages,       ///< Message inbox
    MessageDetail,  ///< One message
    MessageCompose, ///< Compose form
    Count,          ///< Number of screens
};

/** Number of screens, as an array size. */
static constexpr uint8_t VIEW_COUNT = static_cast<uint8_t>(ViewId::Count);

} // namespace ortxui

#endif /* ORTX_UI_VIEWID_HPP */
