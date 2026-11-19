/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_PALETTE_HPP
#define ORTX_UI_PALETTE_HPP

#include "core/graphics.h"

namespace ortxui
{
namespace palette
{

/**
 * Build an opaque colour from 8-bit components.
 *
 * @param r: red.
 * @param g: green.
 * @param b: blue.
 * @return the colour.
 */
constexpr color_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return color_t{ r, g, b, 0xFF };
}

/*
 * Colours of the Standard theme on colour (RGB565) targets.
 */
constexpr color_t BACKGROUND = rgb(0x00, 0x00, 0x00);   ///< App background
constexpr color_t SURFACE = rgb(0x2C, 0x2E, 0x33);      ///< Top bar, elevation
constexpr color_t SURFACE_HIGH = rgb(0x3A, 0x3D, 0x42); ///< Raised, meter track
constexpr color_t SEPARATOR = rgb(0x33, 0x36, 0x3B);    ///< Thin row dividers
constexpr color_t ON_SURFACE = rgb(0xFF, 0xFF, 0xFF);   ///< Primary text
constexpr color_t ON_SURFACE_MUTED = rgb(0x9A, 0x9E, 0xA6); ///< Secondary text
constexpr color_t SELECTION_BLUE = rgb(0x2D, 0x7D, 0xF6);   ///< Selection fill
constexpr color_t BRAND_GOLD = rgb(0xFA, 0xB4, 0x13);       ///< Brand accent
constexpr color_t TX_ORANGE = rgb(0xE8, 0x57, 0x1C);        ///< TX meter, label
constexpr color_t RX_GREEN = rgb(0x35, 0xC8, 0x4B);         ///< RX meter, label
constexpr color_t WARN_ORANGE = rgb(0xF5, 0x73, 0x1E);      ///< Battery low
constexpr color_t MARK_RED = rgb(0xE0, 0x40, 0x1A);         ///< Checklist tick

/* Ink and paper of the High Contrast theme and of 1bpp monochrome targets. */
constexpr color_t BLACK = rgb(0x00, 0x00, 0x00); ///< Paper
constexpr color_t WHITE = rgb(0xFF, 0xFF, 0xFF); ///< Ink

} // namespace palette
} // namespace ortxui

#endif /* ORTX_UI_PALETTE_HPP */
