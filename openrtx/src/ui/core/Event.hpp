/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_EVENT_HPP
#define ORTX_UI_EVENT_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>

namespace ortxui
{

/**
 * Kinds of input/lifecycle event delivered to the widget tree.
 */
enum class EvKind : uint8_t {
    None = 0,  ///< No event
    Key,       ///< Key press; `keys` holds the raw keyboard bitmask
    KeyLong,   ///< Long key press; `keys` holds the raw keyboard bitmask
    KeyRepeat, ///< Auto-repeat of held keys, for handlers that scroll or tune
    Encoder,   ///< Rotary knob step; `encoder` is -1 (left) or +1 (right)
    Status,    ///< Periodic status-change tick (battery, GPS, RTC, ...)
};

/**
 * A decoded UI event, made from a raw event_t whose payload is a kbd_msg_t
 * value. The knob arrives as an Encoder step of -1 or +1.
 */
struct Event {
    /**
     * Decode a raw event.
     *
     * @param type: event_t type.
     * @param payload: event_t payload.
     * @return the event.
     */
    static Event decode(uint8_t type, uint32_t payload);

    EvKind kind = EvKind::None; ///< What happened
    uint32_t keys = 0;          ///< Keyboard bitmask of a key event
    int8_t encoder = 0;         ///< Knob step of an Encoder event
};

} // namespace ortxui

#endif /* ORTX_UI_EVENT_HPP */
