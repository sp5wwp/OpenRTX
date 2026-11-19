/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_SYMBOLS_HPP
#define ORTX_UI_SYMBOLS_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

/*
 * UTF-8 strings of the glyphs the UI draws. Pass them to gfx_print() like any
 * other text. Add a glyph to scripts/gen_fonts.sh before naming it here
 * (docs/fonts.md).
 */

/* FontAwesome icons merged into every text font. */
#define SYMBOL_SIGNAL "\xEF\x80\x92"     ///< U+F012, bandwidth
#define SYMBOL_LOCK "\xEF\x80\xA3"       ///< U+F023, keypad lock
#define SYMBOL_VOLUME "\xEF\x80\xA8"     ///< U+F028, volume and squelch
#define SYMBOL_WARNING "\xEF\x81\xB1"    ///< U+F071, message not sent
#define SYMBOL_ENVELOPE "\xEF\x83\xA0"   ///< U+F0E0, unread message
#define SYMBOL_CHARGE "\xEF\x83\xA7"     ///< U+F0E7, TX power
#define SYMBOL_BELL "\xEF\x83\xB3"       ///< U+F0F3, tone
#define SYMBOL_BRIGHTNESS "\xEF\x86\x85" ///< U+F185, backlight

/* Glyphs of the icon font blobs, the fallback layer of every text font. */

/** The M17 wordmark logo (U+E900), drawn in place of the "M17" mode label. */
#define SYMBOL_M17 "\xEE\xA4\x80"

/** FontAwesome backspace (U+F55A), shown as the character-wheel delete slot. */
#define SYMBOL_BACKSPACE "\xEF\x95\x9A"

/** Ellipsis (U+2026), appended to text truncated to fit its box. */
#define SYMBOL_ELLIPSIS "\xE2\x80\xA6"

#endif /* ORTX_UI_SYMBOLS_HPP */
