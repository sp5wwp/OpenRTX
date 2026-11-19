/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/LayoutFonts.hpp"
#include "core/graphics.h"
#include "core/state.h"

namespace ortxui
{

LayoutFonts::LayoutFonts()
{
    gfx_setFontMono(false);
}

LayoutFonts::~LayoutFonts()
{
    gfx_setFontMono(state.settings.crispText);
}

} // namespace ortxui
