/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * The tuners of a screen of several, built as the RT-950 Pro emulator with
 * three of them.
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

#include "views/TunerView.hpp"
#include "views/UiMemory.hpp"

using namespace ortxui;

static pthread_mutex_t rtxMutex = PTHREAD_MUTEX_INITIALIZER; ///< For rtx_init()

static constexpr freq_t FREQ = 430000000; ///< Frequency the radio is on

/**
 * Queue a key press, run one UI tick, then release the key and run another.
 *
 * @param keys: keys pressed.
 * @return true if a tick asked for the radio configuration to be applied.
 */
static bool pressKeys(uint32_t keys)
{
    bool sync = false;
    for (uint32_t k : { keys, 0u }) {
        kbd_msg_t msg;
        msg.value = 0;
        msg.keys = k;
        REQUIRE(ui_pushEvent(EVENT_KBD, msg.value));
        ui_updateFSM(&sync);
    }
    return sync;
}

/** Start the UI on the tuner screen, with the first tuner selected. */
static void startUi()
{
    rtx_init(&rtxMutex);
    state.settings = default_settings;
    state.charge = 100; /* an empty battery shows the low-battery warning */
    state.channel.mode = OPMODE_FM;
    state.channel.rx_frequency = FREQ;
    state.channel.tx_frequency = FREQ;
    state.tuner_mode = VFO;
    state.pttDisable = false;
    uiMemory.keypadLocked = false;
    uiMemory.selectedTuner = 0;
    ui_init();

    bool sync = false;
    ui_updateFSM(&sync);
}

TEST_CASE("The screen has three tuners, one live", "[ui][tuners]")
{
    REQUIRE(TunerView::TUNER_COUNT == 3);
    REQUIRE(TunerView::LIVE_TUNERS == 1);
}

TEST_CASE("The tuner selection wraps", "[ui][tuners]")
{
    startUi();
    REQUIRE(uiMemory.selectedTuner == 0);

    pressKeys(TunerView::TUNER_SELECT_KEY);
    REQUIRE(uiMemory.selectedTuner == 1);
    pressKeys(TunerView::TUNER_SELECT_KEY);
    REQUIRE(uiMemory.selectedTuner == 2);
    pressKeys(TunerView::TUNER_SELECT_KEY);
    REQUIRE(uiMemory.selectedTuner == 0);
}

TEST_CASE("Placeholders never receive", "[ui][tuners]")
{
    state_t s = state;
    s.rssi = -50;

    for (uint8_t i = 0; i < TunerView::TUNER_COUNT; i++) {
        TunerState t;
        TunerView::describe(i, s, RX, true, t);
        const bool live = (i < TunerView::LIVE_TUNERS);
        REQUIRE(t.placeholder == !live);
        REQUIRE(t.receiving == live);
        REQUIRE_FALSE(t.transmitting);

        TunerState tx;
        TunerView::describe(i, s, TX, false, tx);
        REQUIRE(tx.transmitting == live);
    }
}

TEST_CASE("Keys do nothing on a placeholder", "[ui][tuners]")
{
    startUi();
    pressKeys(TunerView::TUNER_SELECT_KEY);
    REQUIRE_FALSE(TunerView::selectedLive());

    REQUIRE_FALSE(pressKeys(KEY_UP));
    REQUIRE_FALSE(pressKeys(KEY_ESC));
    REQUIRE(state.channel.rx_frequency == FREQ);
    REQUIRE(state.tuner_mode == VFO);

    /* No frequency entry opens: PTT stays blocked by the placeholder only. */
    pressKeys(KEY_1);
    pressKeys(TunerView::TUNER_SELECT_KEY);
    pressKeys(TunerView::TUNER_SELECT_KEY);
    REQUIRE(uiMemory.selectedTuner == 0);
    REQUIRE_FALSE(state.pttDisable);
}

TEST_CASE("PTT is blocked on a placeholder", "[ui][tuners]")
{
    startUi();
    REQUIRE_FALSE(state.pttDisable);

    REQUIRE(pressKeys(TunerView::TUNER_SELECT_KEY));
    REQUIRE(state.pttDisable);

    pressKeys(TunerView::TUNER_SELECT_KEY);
    REQUIRE(state.pttDisable);

    REQUIRE(pressKeys(TunerView::TUNER_SELECT_KEY));
    REQUIRE_FALSE(state.pttDisable);
}
