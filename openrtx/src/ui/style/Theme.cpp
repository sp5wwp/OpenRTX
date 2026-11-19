/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <array>
#include <utility>
#include "style/Theme.hpp"
#include "style/Palette.hpp"
#include "hwconfig.h"
#include "core/state.h"

namespace ortxui
{

/**
 * High Contrast mapping: background roles and text on fills become paper,
 * every other role becomes ink, so selection reads as an inverted fill.
 *
 * @param role: colour role.
 * @return the role's colour.
 */
static constexpr color_t highContrastColor(Sem role)
{
    switch (role) {
        case Sem::Background:
        case Sem::Surface:
        case Sem::SurfaceHigh:
        case Sem::Separator:
        case Sem::OnPrimary:
        case Sem::OnAccent:
            return palette::BLACK;
        default:
            return palette::WHITE;
    }
}

/**
 * Standard mapping.
 *
 * @param role: colour role.
 * @return the role's colour.
 */
static constexpr color_t standardColor(Sem role)
{
    switch (role) {
        case Sem::Background:
            return palette::BACKGROUND;
        case Sem::Surface:
            return palette::SURFACE;
        case Sem::SurfaceHigh:
            return palette::SURFACE_HIGH;
        case Sem::Separator:
            return palette::SEPARATOR;
        case Sem::OnSurface:
            return palette::ON_SURFACE;
        case Sem::OnSurfaceMuted:
            return palette::ON_SURFACE_MUTED;
        case Sem::Primary:
            return palette::SELECTION_BLUE;
        case Sem::OnPrimary:
        case Sem::OnAccent:
            return palette::BLACK;
        case Sem::Accent:
            return palette::BRAND_GOLD;
        case Sem::TxDanger:
            return palette::TX_ORANGE;
        case Sem::RxSuccess:
            return palette::RX_GREEN;
        case Sem::Warning:
            return palette::WARN_ORANGE;
        case Sem::Mark:
            return palette::MARK_RED;
        default:
            return palette::ON_SURFACE;
    }
}

/**
 * Build a theme table from a role mapping, one entry per role index.
 *
 * @param map: role to colour mapping.
 * @return the table.
 */
template <size_t... I>
static constexpr ThemeTable makeTheme(color_t (*map)(Sem),
                                      std::index_sequence<I...>)
{
    return { { { map(static_cast<Sem>(I))... } } };
}

/**
 * Build a theme table from a role mapping, at compile time.
 *
 * @param map: role to colour mapping.
 * @return the table.
 */
static constexpr ThemeTable makeTheme(color_t (*map)(Sem))
{
    return makeTheme(
        map, std::make_index_sequence<static_cast<size_t>(Sem::Count)>());
}

const ThemeTable THEME_STANDARD = makeTheme(standardColor);
const ThemeTable THEME_HIGH_CONTRAST = makeTheme(highContrastColor);

/** Theme installed by themeSetCustom(), or nullptr. */
static const ThemeTable *customTheme = nullptr;

void themeSetCustom(const ThemeTable *theme)
{
    customTheme = theme;
}

color_t themeColor(Sem role)
{
    const ThemeTable *theme = &THEME_HIGH_CONTRAST;

#if !defined(CONFIG_PIX_FMT_BW)
    if (customTheme != nullptr)
        theme = customTheme;
    else if (!state.settings.highContrast)
        theme = &THEME_STANDARD;
#endif

    return theme->colors[static_cast<size_t>(role)];
}

} // namespace ortxui
