/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/Event.hpp"
#include "core/input.h"
#include "core/event.h"
#include "interfaces/keyboard.h"
#include "hwconfig.h"

namespace ortxui
{

Event Event::decode(uint8_t type, uint32_t payload)
{
    Event ev;

    if (type == EVENT_STATUS) {
        ev.kind = EvKind::Status;
        return ev;
    }

    /* EVENT_KBD: payload is a kbd_msg_t value. */
    kbd_msg_t msg;
    msg.value = payload;

    const uint32_t keys = msg.keys;
    if (msg.repeat) {
        ev.kind = EvKind::KeyRepeat;
        ev.keys = keys;
        return ev;
    }

    if (keys & KNOB_LEFT) {
        ev.kind = EvKind::Encoder;
        ev.encoder = -1;
        return ev;
    }
    if (keys & KNOB_RIGHT) {
        ev.kind = EvKind::Encoder;
        ev.encoder = 1;
        return ev;
    }

    ev.kind = msg.long_press ? EvKind::KeyLong : EvKind::Key;
    ev.keys = keys;
    return ev;
}

} // namespace ortxui
