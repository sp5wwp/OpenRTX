/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_THEME_HPP
#define ORTX_UI_THEME_HPP

#include <array>
#include "core/graphics.h"
#include "style/SemanticColor.hpp"

namespace ortxui
{

/**
 * A theme: the colour of each semantic role.
 */
struct ThemeTable {
    std::array<color_t, static_cast<size_t>(Sem::Count)> colors; ///< By Sem
};

/**
 * Built-in "Instrument" colour theme.
 */
extern const ThemeTable THEME_INSTRUMENT;

/**
 * Built-in High Contrast theme: ink and paper only. It is the only theme on
 * 1bpp monochrome targets.
 */
extern const ThemeTable THEME_HIGH_CONTRAST;

/**
 * Use a theme from outside the firmware, e.g. a theme pack, on colour
 * targets. The table is borrowed and must stay valid while installed.
 *
 * @param theme: theme to use, or NULL for the built-in theme picked by
 * settings.highContrast.
 */
void themeSetCustom(const ThemeTable *theme);

/**
 * Resolve a semantic colour role in the active theme. Read on every draw,
 * so a theme change takes effect on the next repaint.
 *
 * @param role: colour role.
 * @return the role's colour.
 */
color_t themeColor(Sem role);

} // namespace ortxui

#endif /* ORTX_UI_THEME_HPP */
