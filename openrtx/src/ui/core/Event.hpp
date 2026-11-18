/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_EVENT_HPP
#define ORTX_UI_EVENT_HPP

#include <cstdint>

namespace ortxui
{

/**
 * Kinds of input/lifecycle event delivered to the widget tree.
 */
enum class EvKind : uint8_t {
    None = 0,
    Key,     //< Key press; `keys` holds the raw keyboard bitmask
    KeyLong, //< Long key press; `keys` holds the raw keyboard bitmask
    Encoder, //< Rotary knob step; `encoder` is -1 (left) or +1 (right)
    Status,  //< Periodic status-change tick (battery, GPS, RTC, ...)
};

/**
 * A decoded UI event. Produced from the raw event queue (event_t type +
 * payload, where the payload is a kbd_msg_t value) and dispatched into the
 * widget tree. The rotary knob is surfaced as an Encoder step so widgets and
 * views can treat it like an LVGL-style encoder input device.
 */
struct Event {
    EvKind kind = EvKind::None;
    uint32_t keys = 0;
    int8_t encoder = 0;

    /**
     * Decode a raw event (event_t.type + payload) into a toolkit Event.
     */
    static Event decode(uint8_t type, uint32_t payload);
};

} // namespace ortxui

#endif /* ORTX_UI_EVENT_HPP */
