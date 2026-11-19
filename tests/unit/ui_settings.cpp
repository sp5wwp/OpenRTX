/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "core/Event.hpp"
#include "core/settings.h"
#include "core/state.h"
#include "interfaces/keyboard.h"
#include "views/ViewSlot.hpp"

using namespace ortxui;

/** A UHF simplex frequency inside the emulator's bands. */
static constexpr freq_t UHF_FREQ = 430000000u;

/**
 * Make a key press event.
 *
 * @param keys: keyboard bitmask.
 * @return the event.
 */
static Event key(uint32_t keys)
{
    Event e;
    e.kind = EvKind::Key;
    e.keys = keys;
    return e;
}

/**
 * Press a key on a view.
 *
 * @param v: view.
 * @param keys: keyboard bitmask.
 */
static void press(View *v, uint32_t keys)
{
    v->onEvent(key(keys));
}

/**
 * Type a number on the keypad.
 *
 * @param v: view.
 * @param digits: digits to type, as text.
 */
static void type(View *v, const char *digits)
{
    for (const char *c = digits; *c != '\0'; c++)
        press(v, 1u << (*c - '0'));
}

/**
 * Reset the settings and tune a simplex UHF channel.
 */
static void resetState()
{
    state.settings = default_settings;
    state.channel.rx_frequency = UHF_FREQ;
    state.channel.tx_frequency = UHF_FREQ;
}

TEST_CASE("Radio offset stays inside the radio's bands", "[ui][settings]")
{
    resetState();
    ViewSlot slot;
    View *v = slot.build(ViewId::Radio);
    v->takeSyncRtx();

    /* Offset is the first row: ENTER opens its keypad entry. */
    press(v, KEY_ENTER);

    SECTION("an offset out of band is refused and the entry stays open")
    {
        type(v, "999999");
        press(v, KEY_ENTER);
        REQUIRE(state.channel.tx_frequency == UHF_FREQ);
        REQUIRE_FALSE(v->takeSyncRtx());

        /* Still editing: a smaller offset now applies. */
        press(v, KEY_STAR);
        press(v, KEY_STAR);
        press(v, KEY_ENTER);
        REQUIRE(state.channel.tx_frequency == UHF_FREQ + 9999000u);
        REQUIRE(v->takeSyncRtx());
    }

    SECTION("an offset in band sets the TX frequency")
    {
        type(v, "5000");
        press(v, KEY_ENTER);
        REQUIRE(state.channel.tx_frequency == UHF_FREQ + 5000000u);
        REQUIRE(v->takeSyncRtx());
    }

    slot.destroy();
}
