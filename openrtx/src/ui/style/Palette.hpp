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
 * Build an opaque color_t from 8-bit R/G/B components.
 */
constexpr color_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return color_t{ r, g, b, 0xFF };
}

/*
 * The "Instrument" raw palette (retuned 2026-07-05 to the reference mockups:
 * true-black body, white type, blue selection, green RX / orange TX, gold kept
 * only as a secondary accent). These are the concrete colours the semantic
 * roles resolve to on colour (RGB565) targets. Iterate live in the linux
 * emulator (true RGB565); do a daylight hardware pass before locking.
 */
constexpr color_t background = rgb(0x00, 0x00, 0x00);   //< true black
constexpr color_t surface = rgb(0x2C, 0x2E, 0x33);      //< top bar / elevation
constexpr color_t surfaceHigh = rgb(0x3A, 0x3D, 0x42);  //< raised / meter track
constexpr color_t separator = rgb(0x33, 0x36, 0x3B);    //< thin row dividers
constexpr color_t onSurface = rgb(0xFF, 0xFF, 0xFF);    //< white
constexpr color_t onSurfaceMut = rgb(0x9A, 0x9E, 0xA6); //< muted grey
constexpr color_t selectionBlue = rgb(0x2D, 0x7D, 0xF6); //< selection / channel
constexpr color_t brandGold = rgb(0xFA, 0xB4, 0x13);     //< OpenRTX 2nd accent
constexpr color_t txOrange = rgb(0xE8, 0x57, 0x1C);      //< TX meter / label
constexpr color_t rxGreen = rgb(0x35, 0xC8, 0x4B);       //< RX meter / label
constexpr color_t warnOrange = rgb(0xF5, 0x73, 0x1E);    //< battery-low
constexpr color_t markRed = rgb(0xE0, 0x40, 0x1A);       //< checklist tick
constexpr color_t m17Red = rgb(0xFF, 0x00, 0x00);        //< M17 brand red
constexpr color_t m17Grey = rgb(0x99, 0x99, 0x99);       //< M17 logo accent
constexpr color_t fmCyanGrey = rgb(0x7F, 0xA6, 0xB8);    //< FM mode badge

/* Ink/paper extremes for 1bpp monochrome targets. */
constexpr color_t black = rgb(0x00, 0x00, 0x00);
constexpr color_t white = rgb(0xFF, 0xFF, 0xFF);

} // namespace palette
} // namespace ortxui

#endif /* ORTX_UI_PALETTE_HPP */
