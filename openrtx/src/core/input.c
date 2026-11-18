/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "interfaces/delays.h"
#include <inttypes.h>
#include <stdbool.h>
#include "core/input.h"

/* Keys that auto-repeat while held (navigation / tuning); every other key keeps
 * its single one-shot long-press. */
#define INPUT_REPEAT_MASK (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)

static long long keyTs[KBD_NUM_KEYS]; // Timestamp of the last event per key
static uint32_t longPressSent;        // Flags to manage long-press events
static keyboard_t prevKeys = 0;       // Previous keyboard status

bool input_scanKeyboard(kbd_msg_t *msg)
{
    msg->value = 0;
    bool kbd_event = false;

    keyboard_t keys = kbd_getKeys();
    long long now = getTick();

    // The key status has changed
    if (keys != prevKeys) {
        // Find newly pressed keys
        keyboard_t newKeys = (keys ^ prevKeys) & keys;

        // Save timestamp
        for (uint8_t k = 0; k < KBD_NUM_KEYS; k++) {
            keyboard_t mask = 1 << k;
            if ((newKeys & mask) != 0) {
                keyTs[k] = now;
                longPressSent &= ~mask;
            }
        }

        // New keyboard event
        msg->keys = keys;
        kbd_event = true;
    }
    // Some key is kept pressed
    else if (keys != 0) {
        // Check the saved timestamps to trigger long-presses and auto-repeat
        for (uint8_t k = 0; k < KBD_NUM_KEYS; k++) {
            keyboard_t mask = 1 << k;
            if ((keys & mask) == 0)
                continue;

            const long long held = now - keyTs[k];
            const bool repeatable = (mask & INPUT_REPEAT_MASK) != 0;

            if ((longPressSent & mask) == 0) {
                // First crossing of the long-press threshold.
                if (held >= input_longPressTimeout) {
                    // Arrows begin auto-repeat (a fresh key event, so every
                    // Key handler steps/scrolls again); other keys fire the
                    // usual one-shot long-press instead.
                    if (!repeatable)
                        msg->long_press = 1;
                    msg->keys = keys;
                    kbd_event = true;
                    longPressSent |= mask;
                    keyTs[k] = now; // measure the next repeat from here
                }
            } else if (repeatable && (held >= input_repeatInterval)) {
                // Subsequent auto-repeat tick for a held arrow.
                msg->keys = keys;
                kbd_event = true;
                keyTs[k] = now;
            }
        }
    }

    prevKeys = keys;

    return kbd_event;
}

bool input_isNumberPressed(kbd_msg_t msg)
{
    return msg.keys & KBD_NUM_MASK;
}

bool input_isCharPressed(kbd_msg_t msg)
{
    return msg.keys & KBD_CHAR_MASK;
}

uint8_t input_getPressedNumber(kbd_msg_t msg)
{
    uint32_t masked_input = msg.keys & KBD_NUM_MASK;
    if (masked_input == 0)
        return 0;

    return __builtin_ctz(msg.keys & KBD_NUM_MASK);
}

uint8_t input_getPressedChar(kbd_msg_t msg)
{
    uint32_t masked_input = msg.keys & KBD_CHAR_MASK;
    if (masked_input == 0)
        return 0;

    return __builtin_ctz(msg.keys & KBD_CHAR_MASK);
}
