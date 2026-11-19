/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>

#include "core/input.h"
#include "style/Charsets.hpp"
#include "widgets/TextInput.hpp"

using namespace ortxui;

static constexpr uint8_t KEY_IDX_2 = 2; ///< Keypad key "2": A B C 2
static constexpr uint8_t KEY_IDX_3 = 3; ///< Keypad key "3": D E F 3

/** Ticks far enough apart that a second tap starts a new character. */
static constexpr long long LATER = input_longPressTimeout + 1;

/**
 * Bind a callsign field to a buffer, as the inline editors do.
 *
 * @param t: field.
 * @param buf: buffer holding the starting text.
 * @param cap: buffer capacity.
 */
static void bindCallsign(TextInput &t, char *buf, uint16_t cap)
{
    t.configure(buf, cap, CHARSET_CALLSIGN, TextInput::Mode::SingleLine,
                FONT_SIZE_8PT);
    t.setMultiTap(MTAP_CALLSIGN);
    t.begin();
}

TEST_CASE("Typing after clearing the field starts from empty",
          "[ui][textinput]")
{
    char buf[10] = "N0CALL";
    TextInput t;
    bindCallsign(t, buf, sizeof(buf));

    t.clear();
    t.tapKey(KEY_IDX_2, 0);
    REQUIRE(strcmp(buf, "A") == 0);

    t.tapKey(KEY_IDX_3, LATER);
    REQUIRE(strcmp(buf, "AD") == 0);

    char out[16];
    t.formatBracketed(out, sizeof(out));
    REQUIRE(strcmp(out, "A[D]") == 0);
}

TEST_CASE("Typing after deleting the last character starts from empty",
          "[ui][textinput]")
{
    char buf[10] = "A";
    TextInput t;
    bindCallsign(t, buf, sizeof(buf));

    t.cycle(-1); /* A -> space */
    t.cycle(-1); /* space -> delete slot */
    t.moveCursor(+1);
    t.tapKey(KEY_IDX_2, 0);
    REQUIRE(strcmp(buf, "A") == 0);
}

TEST_CASE("Moving the cursor ends a multi-tap", "[ui][textinput]")
{
    char buf[10] = "";
    TextInput t;
    bindCallsign(t, buf, sizeof(buf));

    t.tapKey(KEY_IDX_2, 0);
    t.moveCursor(+1);
    t.tapKey(KEY_IDX_2, 1); /* same key, inside the multi-tap window */
    REQUIRE(strcmp(buf, "AA") == 0);

    t.moveCursor(-1);
    t.tapKey(KEY_IDX_2, 2);
    REQUIRE(strcmp(buf, "AA") == 0);
}
