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
#include "interfaces/keyboard.h"
#include "rtx/rtx.h"
}

#include "core/UiMemory.hpp"

static pthread_mutex_t rtxMutex = PTHREAD_MUTEX_INITIALIZER; ///< For rtx_init()

/**
 * Queue a keyboard event and run one UI tick.
 *
 * @param keys: keys held, 0 for a release.
 * @return true if the tick asked for the radio configuration to be applied.
 */
static bool pressKeys(uint32_t keys)
{
    kbd_msg_t msg;
    msg.value = 0;
    msg.keys = keys;
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
}
