/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef RADIO_RT950PRO_H
#define RADIO_RT950PRO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Put the BK4829 that does not serve the transmit frequency's band in or
 * out of its mic codec state, its mic input on its AF pin: the microphone's
 * way to the MCU while the other chip transmits M17.
 *
 * @param enable: true to pass the microphone, false to switch the chip off.
 */
void radio_rt950pro_micCodec(bool enable);

/**
 * Play a tone on the speaker from the chip that is not receiving, or stop it.
 * Nothing is played while transmitting.
 *
 * @param freq: tone frequency in Hz, zero to stop.
 */
void radio_rt950pro_beep(uint16_t freq);

#ifdef __cplusplus
}
#endif

#endif /* RADIO_RT950PRO_H */
