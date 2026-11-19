/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_CHARSETS_HPP
#define ORTX_UI_CHARSETS_HPP

#include <cstdint>
#include "core/Keypad.hpp"

namespace ortxui
{

/**
 * Characters the cursor cycles through when the character under it is changed
 * with the arrows or the knob. Space is first, so a new cell reads as blank.
 */
struct Charset {
    /** Index of `c` in the set, or 0 (space) when absent. */
    int16_t indexOf(char c) const
    {
        for (uint16_t i = 0; i < len; i++)
            if (chars[i] == c)
                return static_cast<int16_t>(i);
        return 0;
    }

    /** Character at `i`, wrapping modulo the set length. */
    char at(int16_t i) const
    {
        const int16_t n = static_cast<int16_t>(len);
        return chars[((i % n) + n) % n];
    }

    const char *chars; ///< Ordered characters
    uint16_t len;      ///< Number of characters, without the NUL
};

/** Callsign and M17 destination: the M17 base-40 alphabet. */
constexpr char CHARS_CALLSIGN[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/.";

/** Free text (M17 meta, SMS): adds lowercase and common punctuation. */
constexpr char CHARS_TEXT[] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
    ".,?!-/@#'()&:;+*=";

constexpr Charset CHARSET_CALLSIGN{ CHARS_CALLSIGN,
                                    sizeof(CHARS_CALLSIGN) - 1 };
constexpr Charset CHARSET_TEXT{ CHARS_TEXT, sizeof(CHARS_TEXT) - 1 };

/**
 * ETSI phone-style multi-tap table for numeric keypads: repeated presses of one
 * key cycle through its string in place; a different key, or a pause of
 * input_longPressTimeout, keeps the character and advances. Indexed by key 0-9,
 * then '*' = 10, '#' = 11. An empty string types nothing ('*' deletes). Space
 * is on '0' or '#', as kbdSpaceOnHash() says the keypad prints it.
 */
struct MultiTapTable {
    static constexpr uint8_t KEY_COUNT = 12; ///< Keys 0-9, '*' and '#'

    const char *keys[KEY_COUNT]; ///< Characters of each key, in tap order
};

/** Callsign: uppercase only, digit last in each group. */
constexpr MultiTapTable MTAP_CALLSIGN = { {
    kbdSpaceOnHash() ? "0" : "0 ", /* 0, and space unless on '#' */
    "1-/.",                        /* 1 and the M17 symbols */
    "ABC2", "DEF3", "GHI4", "JKL5", "MNO6", "PQRS7", "TUV8", "WXYZ9",
    "",                            /* '*' deletes */
    kbdSpaceOnHash() ? " " : "",   /* '#' is space where printed so */
} };

/** Free text: lowercase first, then the digit, then uppercase. */
constexpr MultiTapTable MTAP_TEXT = { {
    kbdSpaceOnHash() ? "0" : " 0",
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
    kbdSpaceOnHash() ? " " : "",
} };

} // namespace ortxui

#endif /* ORTX_UI_CHARSETS_HPP */
