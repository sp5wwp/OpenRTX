/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_SYMBOLS_HPP
#define ORTX_UI_SYMBOLS_HPP

/*
 * UTF-8 glyph strings for toolkit-specific marks baked into the custom icon
 * font at Private-Use-Area code points. These are a UI concern (what a glyph is
 * called), distinct from the font/gfx backend (which just owns glyphs at code
 * points), so they live here rather than in core/graphics.h alongside the
 * generic FontAwesome icon macros.
 *
 * The custom icon font is a fallback layer separate from both the Ubuntu text
 * glyphs and the FontAwesome subset. Its marks are produced from committed SVGs
 * (e.g. openrtx/fonts/m17_logo.svg) by scripts/gen_fonts.sh (see docs/fonts.md)
 * and rendered like any other glyph, so they inherit the anti-aliased blit,
 * sizing, alignment and theme colour of text — single-colour silhouettes that
 * work on colour and 1bpp targets.
 */

/** The M17 wordmark logo (U+E900), drawn in place of the "M17" mode label. */
#define SYMBOL_M17 "\xEE\xA4\x80"

/** FontAwesome backspace (U+F55A), shown as the character-wheel delete slot. */
#define SYMBOL_BACKSPACE "\xEF\x95\x9A"

/** Ellipsis (U+2026), appended to text truncated to fit its box. */
#define SYMBOL_ELLIPSIS "\xE2\x80\xA6"

#endif /* ORTX_UI_SYMBOLS_HPP */
