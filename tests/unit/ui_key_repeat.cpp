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
#include "core/event.h"
#include "core/input.h"
#include "core/state.h"
#include "interfaces/keyboard.h"
#include "app/ViewSlot.hpp"

using namespace ortxui;

static constexpr freq_t FREQ = 430000000; ///< Frequency the VFO starts on

/**
 * Decode a keyboard event as the UI thread does.
 *
 * @param keys: keyboard bitmask.
 * @param repeat: the event is an auto-repeat.
 * @return the event.
 */
static Event kbd(uint32_t keys, bool repeat)
{
    kbd_msg_t msg;
    msg.value = 0;
    msg.keys = keys;
    msg.repeat = repeat ? 1 : 0;
    return Event::decode(EVENT_KBD, msg.value);
}

/**
 * Make a key press event.
 *
 * @param keys: keyboard bitmask.
 * @return the event.
 */
static Event press(uint32_t keys)
{
    return kbd(keys, false);
}

/**
 * Make an auto-repeat event.
 *
 * @param keys: keyboard bitmask.
 * @return the event.
 */
static Event repeat(uint32_t keys)
{
    return kbd(keys, true);
}

/**
 * Put the VFO on a known frequency in VFO mode, with no list remembered.
 */
static void resetVfo()
{
    uiMemory.lists.fill({ 0, 0 });
    state.tuner_mode = VFO;
    state.step_index = 0;
    state.channel.mode = OPMODE_FM;
    state.channel.rx_frequency = FREQ;
    state.channel.tx_frequency = FREQ;
}

TEST_CASE("A repeat decodes apart from a press", "[ui][repeat]")
{
    const Event p = press(KEY_UP);
    REQUIRE(p.kind == EvKind::Key);
    REQUIRE(p.keys == KEY_UP);

    const Event r = repeat(KEY_UP);
    REQUIRE(r.kind == EvKind::KeyRepeat);
    REQUIRE(r.keys == KEY_UP);
}

TEST_CASE("A held arrow keeps scrolling a list", "[ui][repeat]")
{
    ViewSlot slot;
    Navigator nav(slot, uiMemory);
    resetVfo();

    nav.setRoot(ViewId::Vfo);
    nav.dispatch(press(KEY_ENTER));
    REQUIRE(nav.active()->id() == ViewId::MainMenu);

    nav.dispatch(press(KEY_DOWN));
    nav.dispatch(repeat(KEY_DOWN));
    nav.dispatch(repeat(KEY_DOWN));
    REQUIRE(nav.active()->list()->selected() == 3);

    /* ENTER and ESC held do not open or leave anything again. */
    nav.dispatch(repeat(KEY_ENTER));
    nav.dispatch(repeat(KEY_ESC));
    REQUIRE(nav.active()->id() == ViewId::MainMenu);
}

TEST_CASE("A held arrow keeps tuning the VFO", "[ui][repeat]")
{
    ViewSlot slot;
    Navigator nav(slot, uiMemory);
    resetVfo();
    const freq_t step = freq_steps[0];

    nav.setRoot(ViewId::Vfo);
    nav.dispatch(press(KEY_UP));
    nav.dispatch(repeat(KEY_UP));
    nav.dispatch(repeat(KEY_UP));
    REQUIRE(state.channel.rx_frequency == FREQ + 3 * step);

    nav.dispatch(repeat(KEY_DOWN));
    REQUIRE(state.channel.rx_frequency == FREQ + 2 * step);

    /* A held ENTER does not open the menu. */
    nav.dispatch(repeat(KEY_ENTER));
    REQUIRE(nav.active()->id() == ViewId::Vfo);
}

TEST_CASE("The frequency entry ignores repeats", "[ui][repeat]")
{
    ViewSlot slot;
    Navigator nav(slot, uiMemory);
    resetVfo();

    nav.setRoot(ViewId::Vfo);
    nav.dispatch(press(KEY_4)); /* opens the entry, retyping RX */
    nav.dispatch(press(KEY_3));

    /* A held arrow does not flip between RX and TX, nor a held digit type. */
    nav.dispatch(repeat(KEY_UP));
    nav.dispatch(repeat(KEY_UP));
    nav.dispatch(repeat(KEY_UP));
    nav.dispatch(repeat(KEY_3));

    nav.dispatch(press(KEY_5));
    nav.dispatch(press(KEY_ENTER)); /* RX done, TX derived from the offset */
    nav.dispatch(press(KEY_ENTER)); /* commit both */
    REQUIRE(state.channel.rx_frequency == 435000000);
    REQUIRE(state.channel.tx_frequency == 435000000);
}
