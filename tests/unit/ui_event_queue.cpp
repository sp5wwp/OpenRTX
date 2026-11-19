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

#include "views/UiMemory.hpp"

static pthread_mutex_t rtxMutex = PTHREAD_MUTEX_INITIALIZER; ///< For rtx_init()

/**
 * Queue a key event.
 *
 * @param keys: keys held, 0 for a release.
 * @return true if the event was queued.
 */
static bool pushKeys(uint32_t keys)
{
    kbd_msg_t msg;
    msg.value = 0;
    msg.keys = keys;
    return ui_pushEvent(EVENT_KBD, msg.value);
}

TEST_CASE("Key presses queue behind any number of status updates", "[ui]")
{
    rtx_init(&rtxMutex);
    state.settings = default_settings;
    state.charge = 100; /* an empty battery shows the low-battery warning */
    ortxui::uiMemory.keypadLocked = false;
    state.pttDisable = false;
    ui_init();

    /* The state thread keeps posting while the UI thread runs behind. */
    for (size_t i = 0; i < (2 * UI_EVENT_QUEUE_LEN); i++)
        REQUIRE(ui_pushEvent(EVENT_STATUS, 0));

    REQUIRE(pushKeys(KEY_ENTER));
    REQUIRE(pushKeys(0));

    /* One status event, then the press, which opens the main menu. */
    bool sync = false;
    ui_updateFSM(&sync);
    REQUIRE_FALSE(state.pttDisable);
    ui_updateFSM(&sync);
    REQUIRE(state.pttDisable);

    ui_updateFSM(&sync);
    REQUIRE(pushKeys(KEY_ESC));
    ui_updateFSM(&sync);
    REQUIRE_FALSE(state.pttDisable);
}
