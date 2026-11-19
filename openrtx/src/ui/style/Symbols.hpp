/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_SYMBOLS_HPP
#define ORTX_UI_SYMBOLS_HPP

/*
 * UTF-8 strings of the glyphs the UI draws from the icon font blobs, the
 * fallback layer of every text font (docs/fonts.md). The FontAwesome icons
 * merged into the text fonts are named in core/graphics.h.
 */

/** The M17 wordmark logo (U+E900), drawn in place of the "M17" mode label. */
#define SYMBOL_M17 "\xEE\xA4\x80"

/** FontAwesome backspace (U+F55A), shown as the character-wheel delete slot. */
#define SYMBOL_BACKSPACE "\xEF\x95\x9A"

/** Ellipsis (U+2026), appended to text truncated to fit its box. */
#define SYMBOL_ELLIPSIS "\xE2\x80\xA6"

#endif /* ORTX_UI_SYMBOLS_HPP */
