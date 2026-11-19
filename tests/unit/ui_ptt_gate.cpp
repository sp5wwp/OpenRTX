/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>
#include <pthread.h>

extern "C" {
#include "core/event.h"
#include "core/input.h"
#include "core/settings.h"
#include "core/state.h"
#include "core/ui.h"
#include "emulator/emulator.h"
#include "interfaces/keyboard.h"
#include "rtx/rtx.h"
}

#include "views/UiMemory.hpp"

static pthread_mutex_t rtxMutex = PTHREAD_MUTEX_INITIALIZER; ///< For rtx_init()

/**
 * Queue a keyboard event and run one UI tick.
 *
 * @param keys: keys held, 0 for a release.
 * @param longPress: report the keys as held past the long-press timeout.
 * @return true if the tick asked for the radio configuration to be applied.
 */
static bool pressKeys(uint32_t keys, bool longPress = false)
{
    kbd_msg_t msg;
    msg.value = 0;
    msg.keys = keys;
    msg.long_press = longPress ? 1 : 0;
    REQUIRE(ui_pushEvent(EVENT_KBD, msg.value));

    bool sync = false;
    ui_updateFSM(&sync);
    return sync;
}

TEST_CASE("PTT is gated outside the home screen", "[ui]")
{
    /* The UI tick reads the squelch through rtx; no platform state is needed. */
    rtx_init(&rtxMutex);
    state.settings = default_settings;
    state.charge = 100; /* an empty battery shows the low-battery warning */
    ortxui::uiMemory.keypadLocked = false;
    state.pttDisable = false;
    ui_init();

    bool sync = false;
    ui_updateFSM(&sync);
    REQUIRE_FALSE(state.pttDisable);

    SECTION("a menu blocks transmission until going back")
    {
        REQUIRE(pressKeys(KEY_ENTER));
        REQUIRE(state.pttDisable);

        REQUIRE(pressKeys(KEY_ESC));
        REQUIRE_FALSE(state.pttDisable);
    }

    SECTION("the macro menu blocks transmission while open")
    {
        REQUIRE(pressKeys(KEY_MONI));
        REQUIRE(state.pttDisable);

        REQUIRE(pressKeys(0));
        REQUIRE_FALSE(state.pttDisable);
    }

    SECTION("a frequency entry blocks transmission until it closes")
    {
        pressKeys(KEY_1);
        REQUIRE(state.pttDisable);

        pressKeys(0);
        pressKeys(KEY_ESC);
        REQUIRE_FALSE(state.pttDisable);
    }

    SECTION("keying up closes a latched macro menu")
    {
        state.settings.macroMenuLatch = 1;
        pressKeys(KEY_MONI);
        pressKeys(KEY_MONI, true);
        pressKeys(0);
        REQUIRE(state.pttDisable);

        emulator_state.PTTstatus = true;
        ui_updateFSM(&sync);
        emulator_state.PTTstatus = false;
        REQUIRE_FALSE(state.pttDisable);
    }
}

TEST_CASE("Keypad lock lets the FM tone burst through", "[ui]")
{
    rtx_init(&rtxMutex);
    state.settings = default_settings;
    state.charge = 100; /* an empty battery shows the low-battery warning */
    state.channel.mode = OPMODE_FM;
    state.channel.rx_frequency = 430000000;
    state.channel.tx_frequency = 430000000;
    state.tone_enabled = false;
    ui_init();
    ortxui::uiMemory.keypadLocked = true;

    pressKeys(KEY_UP);
    pressKeys(0);
    REQUIRE(state.channel.rx_frequency == 430000000);

    pressKeys(KEY_HASH);
    REQUIRE(state.tone_enabled);

    pressKeys(0);
    REQUIRE_FALSE(state.tone_enabled);

    ortxui::uiMemory.keypadLocked = false;
}
