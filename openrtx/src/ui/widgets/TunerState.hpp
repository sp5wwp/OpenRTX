/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_TUNERSTATE_HPP
#define ORTX_UI_TUNERSTATE_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>

namespace ortxui
{

/**
 * What one tuner shows: the plain model a Tuner draws from. The screen fills
 * it on the stack before each frame, from the radio state for a tuner the
 * radio follows, as a placeholder for the others.
 */
struct TunerState {
    const char *mode = "";      ///< Mode label, a literal or a table entry
    const char *detailTag = ""; ///< "CAN" or "CTCSS", empty for none
    uint32_t rxFreq = 0;        ///< RX frequency in Hz
    uint32_t txFreq = 0;        ///< TX frequency in Hz
    uint32_t txPower = 0;       ///< TX power in mW
    int32_t rssi = 0;           ///< RSSI in dBm
    char index[8] = {};         ///< "VFO" or the memory index
    char name[40] = {};         ///< Channel name or M17 destination
    char detail[8] = {};        ///< CAN (M17) or CTCSS tone (FM) value
    uint8_t squelch = 0;        ///< Squelch level, marked on the RX meter
    uint8_t opMode = 0;         ///< Operating mode: FM marks the squelch
    bool selected = false;      ///< Keys act on this tuner
    bool receiving = false;     ///< The squelch is open on this tuner
    bool transmitting = false;  ///< The radio transmits on this tuner
    bool placeholder = false;   ///< No receiver behind it yet
};

} // namespace ortxui

#endif /* ORTX_UI_TUNERSTATE_HPP */
