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
            return palette::black;
        default:
            return palette::white;
    }
}

/**
 * Instrument mapping.
 *
 * @param role: colour role.
 * @return the role's colour.
 */
static constexpr color_t instrumentColor(Sem role)
{
    switch (role) {
        case Sem::Background:
            return palette::background;
        case Sem::Surface:
            return palette::surface;
        case Sem::SurfaceHigh:
            return palette::surfaceHigh;
        case Sem::Separator:
            return palette::separator;
        case Sem::OnSurface:
            return palette::onSurface;
        case Sem::OnSurfaceMuted:
            return palette::onSurfaceMut;
        case Sem::Primary:
            return palette::selectionBlue;
        case Sem::OnPrimary:
        case Sem::OnAccent:
            return palette::black;
        case Sem::Accent:
            return palette::brandGold;
        case Sem::TxDanger:
            return palette::txOrange;
        case Sem::RxSuccess:
            return palette::rxGreen;
        case Sem::Warning:
            return palette::warnOrange;
        case Sem::Mark:
            return palette::markRed;
        default:
            return palette::onSurface;
    }
}

/**
 * Build a theme table from a role mapping and the list of role indices.
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

const ThemeTable THEME_INSTRUMENT = makeTheme(instrumentColor);
const ThemeTable THEME_HIGH_CONTRAST = makeTheme(highContrastColor);

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
        theme = &THEME_INSTRUMENT;
#endif

    return theme->colors[static_cast<size_t>(role)];
}

} // namespace ortxui
