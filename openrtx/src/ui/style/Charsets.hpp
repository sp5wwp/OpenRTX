/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_CHARSETS_HPP
#define ORTX_UI_CHARSETS_HPP

#include <cstdint>
#include "hwconfig.h"

namespace ortxui
{

/**
 * A character set ("keyboard") for the cursor-cycle text input. The platform
 * gives only a raw key bitmask (no T9 / tap-repeat), so text is entered by
 * cycling the character under the cursor through one of these ordered sets.
 * Space is first so a fresh slot reads as blank and cycles up into letters.
 */
struct Charset {
    const char *chars; //< ordered characters, NUL-terminated string body
    uint16_t len;      //< number of characters (excludes the NUL)
    const char *name;  //< short label (for a future keyboard-picker UI)

    /** Index of `c` in the set, or 0 (space) when absent. */
    int indexOf(char c) const
    {
        for (uint16_t i = 0; i < len; i++)
            if (chars[i] == c)
                return static_cast<int>(i);
        return 0;
    }

    /** Character at `i`, wrapping modulo the set length (handles cycling). */
    char at(int i) const
    {
        const int n = static_cast<int>(len);
        return chars[((i % n) + n) % n];
    }
};

/* Callsign / M17 destination: space + uppercase + digits + a few separators
 * (the historic callsign alphabet). namespace-scope constexpr has internal
 * linkage, so each translation unit gets its own read-only copy (no C++17
 * inline-variable dependency). */
constexpr char kCharsCallsign[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/.";

/* Free text (M17 meta, SMS): adds lowercase and common punctuation. */
constexpr char kCharsText[] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
    ".,?!-/@#'()&:;+*=";

/* Numeric / DTMF. */
constexpr char kCharsNumeric[] = "0123456789*#";

constexpr Charset CHARSET_CALLSIGN{ kCharsCallsign, sizeof(kCharsCallsign) - 1,
                                    "Callsign" };
constexpr Charset CHARSET_TEXT{ kCharsText, sizeof(kCharsText) - 1, "Text" };
constexpr Charset CHARSET_NUMERIC{ kCharsNumeric, sizeof(kCharsNumeric) - 1,
                                   "Numeric" };

/**
 * ETSI phone-style multi-tap table for numeric-keypad radios: repeated presses
 * of one key cycle through its string (in place); a different key or a >700ms
 * pause commits the character and advances. Indexed by key 0-9, then '*' = 10,
 * '#' = 11. An empty string means the key produces no character (it is an
 * action key — '*' is backspace, handled by the host). The space character
 * lives on '0' or '#' per the target's CONFIG_KBD_SPACE_ON_HASH silkscreen.
 */
struct MultiTapTable {
    const char *keys[12];
};

/* Callsign (M17 base-40: A-Z 0-9 - / . and space). Uppercase only, digit last
 * in each group so a quick tap gives the letter. */
constexpr MultiTapTable MTAP_CALLSIGN = { {
    CONFIG_KBD_SPACE_ON_HASH ? "0" : "0 ", //< 0 (+ space unless space is on #)
    "1-/.",                                //< 1 and the M17 symbols
    "ABC2", "DEF3", "GHI4", "JKL5", "MNO6", "PQRS7", "TUV8", "WXYZ9",
    "",                                    //< '*' = backspace (host action)
    CONFIG_KBD_SPACE_ON_HASH ? " " : "",   //< '#' = space when space is on #
} };

/* Free text (M17 meta / SMS): lowercase-first, then uppercase, then digit. */
constexpr MultiTapTable MTAP_TEXT = { {
    CONFIG_KBD_SPACE_ON_HASH ? "0" : " 0",
    ".,?!1-/@",
    "abc2ABC",
    "def3DEF",
    "ghi4GHI",
    "jkl5JKL",
    "mno6MNO",
    "pqrs7PQRS",
    "tuv8TUV",
    "wxyz9WXYZ",
    "",
    CONFIG_KBD_SPACE_ON_HASH ? " " : "",
} };

} // namespace ortxui

#endif /* ORTX_UI_CHARSETS_HPP */
