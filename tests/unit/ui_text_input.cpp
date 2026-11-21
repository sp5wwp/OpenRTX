/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>

#include "base/Event.hpp"
#include "app/Navigator.hpp"
#include "views/UiMemory.hpp"
#include "core/input.h"
#include "core/state.h"
#include "interfaces/keyboard.h"
#include "widgets/Charsets.hpp"
#include "app/ViewSlot.hpp"
#include "widgets/TextInput.hpp"

using namespace ortxui;

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

/**
 * Make an event.
 *
 * @param kind: event kind.
 * @param keys: keyboard bitmask.
 * @return the event.
 */
static Event event(EvKind kind, uint32_t keys)
{
    Event e;
    e.kind = kind;
    e.keys = keys;
    return e;
}

/**
 * Make a key press event.
 *
 * @param keys: keyboard bitmask.
 * @return the event.
 */
static Event press(uint32_t keys)
{
    return event(EvKind::Key, keys);
}

TEST_CASE("Typing after clearing the field starts from empty",
          "[ui][textinput]")
{
    char buf[10] = "N0CALL";
    TextInput t;
    bindCallsign(t, buf, sizeof(buf));

    t.handleKey(KEY_HASH, 0);
    t.handleKey(KEY_2, 0);
    REQUIRE(strcmp(buf, "A") == 0);

    t.handleKey(KEY_3, LATER);
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

    t.handleKey(KEY_DOWN, 0); /* A -> space */
    t.handleKey(KEY_DOWN, 0); /* space -> delete slot */
    t.handleKey(KEY_RIGHT, 0);
    t.handleKey(KEY_2, 0);
    REQUIRE(strcmp(buf, "A") == 0);
}

TEST_CASE("Moving the cursor ends a multi-tap", "[ui][textinput]")
{
    char buf[10] = "";
    TextInput t;
    bindCallsign(t, buf, sizeof(buf));

    t.handleKey(KEY_2, 0);
    t.handleKey(KEY_RIGHT, 0);
    t.handleKey(KEY_2, 1); /* same key, inside the multi-tap window */
    REQUIRE(strcmp(buf, "AA") == 0);

    t.handleKey(KEY_LEFT, 0);
    t.handleKey(KEY_2, 2);
    REQUIRE(strcmp(buf, "AA") == 0);
}

TEST_CASE("A picked character is one cell", "[ui][textinput]")
{
    char buf[10] = "AB";
    TextInput t;
    bindCallsign(t, buf, sizeof(buf));

    t.putCodePoint(0xE9); /* e acute */
    REQUIRE(memcmp(buf, "\xC3\xA9", 2) == 0);
    REQUIRE(strcmp(buf + 2, "B") == 0);

    char out[16];
    t.formatBracketed(out, sizeof(out));
    REQUIRE(strcmp(out, "[\xC3\xA9]B") == 0);

    t.handleKey(KEY_RIGHT, 0);
    t.formatBracketed(out, sizeof(out));
    REQUIRE(strcmp(out, "\xC3\xA9[B]") == 0);
}

TEST_CASE("A text field follows the editor key contract", "[ui][textinput]")
{
    char buf[10] = "AB1CD";
    TextInput t;
    bindCallsign(t, buf, sizeof(buf));
    t.resume(4, 0); /* on the 'D' */

    SECTION("a held star keeps deleting")
    {
        REQUIRE(t.handleEvent(press(KEY_STAR), 0)
                == TextInput::KeyResult::Editing);
        REQUIRE(t.handleEvent(event(EvKind::KeyRepeat, KEY_STAR), 0)
                == TextInput::KeyResult::Editing);
        REQUIRE(strcmp(buf, "ABD") == 0);
    }

    SECTION("a held digit types once")
    {
        t.handleEvent(press(KEY_2), 0);
        REQUIRE(t.handleEvent(event(EvKind::KeyRepeat, KEY_2), 0)
                == TextInput::KeyResult::NotHandled);
        REQUIRE(strcmp(buf, "AB1CA") == 0);
    }

    SECTION("hash clears the field")
    {
        t.handleEvent(press(KEY_HASH), 0);
        t.handleEvent(press(KEY_3), 0);
        REQUIRE(strcmp(buf, "D") == 0);
    }

    SECTION("a long star or hash does nothing more")
    {
        REQUIRE(t.handleEvent(event(EvKind::KeyLong, KEY_STAR), 0)
                == TextInput::KeyResult::NotHandled);
        REQUIRE(t.handleEvent(event(EvKind::KeyLong, KEY_HASH), 0)
                == TextInput::KeyResult::NotHandled);
        REQUIRE(strcmp(buf, "AB1CD") == 0);
    }

    SECTION("enter commits without the trailing blank")
    {
        t.handleEvent(press(KEY_RIGHT), 0);
        REQUIRE(t.handleEvent(press(KEY_ENTER), 0)
                == TextInput::KeyResult::Committed);
        REQUIRE(strcmp(buf, "AB1CD") == 0);
    }

    SECTION("escape cancels")
    {
        REQUIRE(t.handleEvent(press(KEY_ESC), 0)
                == TextInput::KeyResult::Cancelled);
    }
}

TEST_CASE("Escape restores the callsign being edited", "[ui][textinput]")
{
    ViewSlot slot;
    Navigator nav(slot, uiMemory);
    uiMemory.lists.fill({ 0, 0 });
    strcpy(state.settings.callsign, "N0CALL");

    nav.setRoot(ViewId::Tuner);
    nav.openModal(ViewId::M17);
    nav.dispatch(press(KEY_ENTER)); /* edit the callsign */
    nav.dispatch(press(KEY_2));
    nav.dispatch(press(KEY_ESC));
    REQUIRE(strcmp(state.settings.callsign, "N0CALL") == 0);

    nav.dispatch(press(KEY_ENTER));
    nav.dispatch(press(KEY_2));
    nav.dispatch(press(KEY_ENTER));
    REQUIRE(strcmp(state.settings.callsign, "A0CALL") == 0);
}

TEST_CASE("Only a long digit opens the character picker", "[ui][textinput]")
{
    ViewSlot slot;
    Navigator nav(slot, uiMemory);
    uiMemory.lists.fill({ 0, 0 });
    strcpy(state.settings.M17_meta_text, "ABC");

    nav.setRoot(ViewId::Tuner);
    nav.openModal(ViewId::M17);
    nav.dispatch(press(KEY_DOWN)); /* Meta Txt */
    nav.dispatch(press(KEY_ENTER));
    REQUIRE(nav.active()->id() == ViewId::TextInput);

    nav.dispatch(press(KEY_STAR));
    nav.dispatch(event(EvKind::KeyLong, KEY_STAR));
    nav.dispatch(press(KEY_HASH));
    nav.dispatch(event(EvKind::KeyLong, KEY_HASH));
    REQUIRE(nav.active()->id() == ViewId::TextInput);

    nav.dispatch(press(KEY_2));
    nav.dispatch(event(EvKind::KeyLong, KEY_2));
    REQUIRE(nav.active()->id() == ViewId::Picker);

    /* Leaving the picker and then the editor drops the edit. */
    nav.dispatch(press(KEY_ESC));
    nav.dispatch(press(KEY_ESC));
    REQUIRE(nav.active()->id() == ViewId::M17);
    REQUIRE(strcmp(state.settings.M17_meta_text, "ABC") == 0);
}
