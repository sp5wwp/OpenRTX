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
#include "interfaces/platform.h"
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

TEST_CASE("Settings that reach the radio request an rtx sync", "[ui][settings]")
{
    resetState();
    ViewSlot slot;

    SECTION("squelch")
    {
        View *v = slot.build(ViewId::Display);
        /* Squelch is the row after Brightness. */
        press(v, KEY_DOWN);
        press(v, KEY_ENTER);
        REQUIRE_FALSE(v->takeSyncRtx());
        press(v, KEY_UP);
        REQUIRE(state.settings.sqlLevel == default_settings.sqlLevel + 1);
        REQUIRE(v->takeSyncRtx());
    }

    SECTION("M17 callsign")
    {
        View *v = slot.build(ViewId::M17);
        press(v, KEY_ENTER);
        press(v, KEY_UP);
        press(v, KEY_ENTER);
        REQUIRE(v->takeSyncRtx());
    }

    SECTION("M17 CAN")
    {
        View *v = slot.build(ViewId::M17);
        press(v, KEY_DOWN);
        press(v, KEY_DOWN);
        press(v, KEY_ENTER);
        press(v, KEY_UP);
        REQUIRE(state.settings.m17_can == 1);
        REQUIRE(v->takeSyncRtx());
    }

    SECTION("M17 CAN RX check")
    {
        View *v = slot.build(ViewId::M17);
        press(v, KEY_DOWN);
        press(v, KEY_DOWN);
        press(v, KEY_DOWN);
        press(v, KEY_ENTER);
        REQUIRE(state.settings.m17_can_rx);
        REQUIRE(v->takeSyncRtx());
    }

    slot.destroy();
}

TEST_CASE("Accessibility steps the voice prompt level", "[ui][settings]")
{
    resetState();
    ViewSlot slot;
    View *v = slot.build(ViewId::Accessibility);

    /* Voice is the row after Macro Latch. */
    press(v, KEY_DOWN);
    press(v, KEY_ENTER);
    press(v, KEY_DOWN);
    REQUIRE(state.settings.vpLevel == vpNone);
    press(v, KEY_UP);
    press(v, KEY_UP);
    REQUIRE(state.settings.vpLevel == vpLow);
    press(v, KEY_UP);
    press(v, KEY_UP);
    press(v, KEY_UP);
    REQUIRE(state.settings.vpLevel == vpHigh);

    slot.destroy();
}

TEST_CASE("Time and date sets only a valid entry", "[ui][settings]")
{
    resetState();
    state.time = {};
    ViewSlot slot;
    View *v = slot.build(ViewId::TimeDate);
    press(v, KEY_ENTER);

    SECTION("a day past the end of its month is refused")
    {
        type(v, "3002241345");
        press(v, KEY_ENTER);
        REQUIRE(state.time.month == 0);
    }

    SECTION("an hour past 23 is refused")
    {
        type(v, "2902242445");
        press(v, KEY_ENTER);
        REQUIRE(state.time.month == 0);
    }

    SECTION("a leap day is set, on the clock too")
    {
        type(v, "2902241345");
        press(v, KEY_ENTER);
        REQUIRE(state.time.date == 29);
        REQUIRE(state.time.month == 2);
        REQUIRE(state.time.year == 24);
        REQUIRE(state.time.hour == 13);
        REQUIRE(state.time.minute == 45);

        const datetime_t rtc = platform_getCurrentTime();
        REQUIRE(rtc.date == 29);
        REQUIRE(rtc.month == 2);
        REQUIRE(rtc.year == 24);
        REQUIRE(rtc.hour == 13);
    }

    slot.destroy();
}
