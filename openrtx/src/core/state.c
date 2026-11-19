/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/ui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "core/event.h"
#include "core/state.h"
#include "core/battery.h"
#include "hwconfig.h"
#include "interfaces/platform.h"
#include "interfaces/nvmem.h"
#include "interfaces/delays.h"
#ifdef CONFIG_NVM_SAVE_ON_CHANGE
#include "core/crc.h"
#endif

state_t state;
pthread_mutex_t state_mutex;
static long long int lastUpdate = 0;

// Commonly used frequency steps, expressed in Hz
const uint32_t freq_steps[] = { 1000,  5000,  6250,  10000, 12500,
                                15000, 20000, 25000, 50000, 100000 };
const size_t n_freq_steps = sizeof(freq_steps) / sizeof(freq_steps[0]);

void state_init()
{
    pthread_mutex_init(&state_mutex, NULL);

    /*
     * Try loading settings from nonvolatile memory and default to sane values
     * in case of failure.
     */
    if (nvm_readSettings(&state.settings) < 0) {
        state.settings = default_settings;
    }

    /*
     * Try loading VFO configuration from nonvolatile memory and default to sane
     * values in case of failure.
     */
    if (nvm_readVfoChannelData(&state.channel) < 0) {
        state.channel = cps_getDefaultChannel();
    }

/*
     * Initialise remaining fields
     */
#ifdef CONFIG_RTC
    state.time = platform_getCurrentTime();
#endif
    state.v_bat = platform_getVbat();
    state.charge = battery_getCharge(state.v_bat);
    state.rssi = -127.0f;
    state.volume = platform_getVolumeLevel();

    state.channel_index = 0; // Set default channel index (it is 0-based)
    state.bank_enabled = false;
    state.rtxStatus = RTX_OFF;
    state.emergency = false;
    state.txDisable = false;
    state.step_index = 4; // Default frequency step 12.5kHz

    // Force brightness field to be in range 0 - 100
    if (state.settings.brightness > 100) {
        state.settings.brightness = 100;
    }
}

void state_terminate()
{
    // Never store a brightness of 0 to avoid booting with a black screen
    if (state.settings.brightness == 0) {
        state.settings.brightness = 5;
    }

    nvm_writeSettingsAndVfo(&state.settings, &state.channel);
    pthread_mutex_destroy(&state_mutex);
}

#ifdef CONFIG_NVM_SAVE_ON_CHANGE
/*
 * Settings and VFO saved some seconds after they last changed, for radios
 * where saving at power off is not reliable.
 */
#define SAVE_CHECK_MS 1000
#define SAVE_DELAY_MS 5000
#define SAVE_RETRY_MS 60000 /* After a failed save */

uint32_t state_nvmSaves;
static uint32_t savedCrc;   /* What is stored, or was loaded at boot */
static uint32_t pendingCrc; /* Changed contents waiting to settle */
static long long pendingSince;
static long long lastCheck;
static bool crcKnown;

static void saveOnChange()
{
    long long now = getTick();
    if ((now - lastCheck) < SAVE_CHECK_MS)
        return;
    lastCheck = now;

    settings_t settings;
    channel_t vfo;
    pthread_mutex_lock(&state_mutex);
    settings = state.settings;
    vfo = state.channel;
    pthread_mutex_unlock(&state_mutex);

    // Never store a brightness of 0, as in state_terminate()
    if (settings.brightness == 0)
        settings.brightness = 5;

    uint32_t crc = ((uint32_t)crc_ccitt(&settings, sizeof(settings)) << 16)
                 | crc_ccitt(&vfo, sizeof(vfo));
    if (!crcKnown) {
        savedCrc = pendingCrc = crc;
        crcKnown = true;
        return;
    }
    if (crc == savedCrc)
        return;
    if (crc != pendingCrc) {
        pendingCrc = crc;
        pendingSince = now;
        return;
    }
    if ((now - pendingSince) < SAVE_DELAY_MS)
        return;

    if (nvm_writeSettingsAndVfo(&settings, &vfo) == 0) {
        savedCrc = crc;
        state_nvmSaves++;
    } else {
        /* Do not hammer the flash: try again later */
        pendingSince = now + SAVE_RETRY_MS - SAVE_DELAY_MS;
    }
}
#endif

void state_task()
{
    // Update radio state once every 100ms
    if ((getTick() - lastUpdate) < 100)
        return;

    lastUpdate = getTick();

    pthread_mutex_lock(&state_mutex);

    /*
     * Low-pass filtering with a time constant of 10s when updated at 1Hz
     * Original computation: state.v_bat = 0.02*vbat + 0.98*state.v_bat
     * Peak error is 18mV when input voltage is 49mV.
     *
     * NOTE: GD77 and DM-1801 already have an hardware low-pass filter on the
     * vbat pin. Adding also the digital one seems to cause more troubles than
     * benefits.
     */
    uint16_t vbat = platform_getVbat();
#if defined(PLATFORM_GD77) || defined(PLATFORM_DM1801)
    state.v_bat = vbat;
#else
    state.v_bat -= (state.v_bat * 2) / 100;
    state.v_bat += (vbat * 2) / 100;
#endif

    /*
     * Update volume level, as a 50% average between previous value and a new
     * read of the knob position. This gives a good reactivity while preventing
     * the volume level to jitter when the knob is not being moved.
     */
    uint16_t vol = platform_getVolumeLevel() + state.volume;
    state.volume = vol / 2;

    state.charge = battery_getCharge(state.v_bat);
    state.rssi = rtx_getRssi();

#ifdef CONFIG_RTC
    state.time = platform_getCurrentTime();
#endif

    pthread_mutex_unlock(&state_mutex);

    ui_pushEvent(EVENT_STATUS, 0);

#ifdef CONFIG_NVM_SAVE_ON_CHANGE
    saveOnChange();
#endif
}

void state_resetSettingsAndVfo()
{
    state.settings = default_settings;
    state.channel = cps_getDefaultChannel();
}
