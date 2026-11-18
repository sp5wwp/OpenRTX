/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

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
        case Sem::FocusRing:
            return palette::brandGold;
        case Sem::TxDanger:
            return palette::txOrange;
        case Sem::RxSuccess:
        case Sem::ModeDMR:
            return palette::rxGreen;
        case Sem::Warning:
            return palette::warnOrange;
        case Sem::Mark:
            return palette::markRed;
        case Sem::ModeM17:
            return palette::m17Red;
        case Sem::ModeM17Accent:
            return palette::m17Grey;
        case Sem::ModeFM:
            return palette::fmCyanGrey;
        default:
            return palette::onSurface;
    }
}

/**
 * Build a theme table from a role mapping.
 *
 * @param map: role to colour mapping.
 * @return the table.
 */
static constexpr ThemeTable makeTheme(color_t (*map)(Sem))
{
    ThemeTable t{};
    for (int i = 0; i < static_cast<int>(Sem::Count); i++)
        t.colors[i] = map(static_cast<Sem>(i));
    return t;
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

    int i = static_cast<int>(role);
    if ((i < 0) || (i >= static_cast<int>(Sem::Count)))
        i = static_cast<int>(Sem::OnSurface);

    return theme->colors[i];
}

} // namespace ortxui
