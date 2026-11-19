/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Number of slots of the UI event queue.
 */
#define UI_EVENT_QUEUE_LEN 16

/**
 * This function initialises the User Interface, starting the
 * Finite State Machine describing the user interaction.
 */
void ui_init();

/**
 * This function draws the OpenRTX splash screen image centered in the screen
 * space and sends it to the display.
 */
void ui_drawSplashScreen();

/**
 * Copy the radio state the UI draws from. Call it with state_mutex held.
 */
void ui_saveState();

/**
 * This function advances the User Interface FSM, basing on the
 * current radio state and the keys pressed. Call it with state_mutex held.
 *
 * @param sync_rtx: If true RTX needs to be synchronized
 */
void ui_updateFSM(bool *sync_rtx);

/**
 * This function redraws the GUI based on the last radio state and sends it to
 * the display, when it changed.
 */
void ui_updateGUI();

/**
 * Push an event to the UI event queue. Call it with state_mutex held, as
 * several threads push and ui_updateFSM() pops under that lock.
 *
 * @param type: event type.
 * @param data: event data.
 * @return true on success false on failure.
 */
bool ui_pushEvent(const uint8_t type, const uint32_t data);

/**
 * This function terminates the User Interface.
 */
void ui_terminate();

#ifdef __cplusplus
}
#endif

#endif /* UI_H */
