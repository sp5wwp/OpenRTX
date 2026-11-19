/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_UIFORMAT_HPP
#define ORTX_UI_UIFORMAT_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>
#include "core/UiText.hpp"
#include "core/cps.h"

namespace ortxui
{

/**
 * Get the label of an FM channel's CTCSS use: None, Encode (the TX tone),
 * Decode (the RX tone) or Both.
 *
 * @param fm: FM channel settings.
 * @return the label.
 */
UiText toneEnableLabel(const fmInfo_t &fm);

/**
 * Step an FM channel's CTCSS use through None, Decode, Encode and Both.
 *
 * @param fm: FM channel settings.
 * @param dir: +1 forward, -1 back.
 */
void stepToneEnable(fmInfo_t &fm, int8_t dir);

/**
 * Step an FM channel's CTCSS tone through the tone table, wrapping at
 * either end; the RX tone follows the TX tone.
 *
 * @param fm: FM channel settings.
 * @param dir: +1 up, -1 down.
 */
void stepTone(fmInfo_t &fm, int8_t dir);

/**
 * Format a CTCSS tone in Hz, e.g. "88.5".
 *
 * @param index: tone index in ctcss_tone.
 * @param out: output buffer.
 * @param n: output buffer size.
 */
void formatTone(uint8_t index, char *out, size_t n);

/**
 * Format a TX power, e.g. "2.5W" or "500mW".
 *
 * @param mw: power in mW.
 * @param out: output buffer.
 * @param n: output buffer size.
 */
void formatPower(uint32_t mw, char *out, size_t n);

/** Highest TX power the macro menu sets, in mW. */
static constexpr uint32_t POWER_MAX_MW = 5000;

/**
 * Get the TX power after another in the macro menu's cycle: 1, 2.5, 5 W.
 *
 * @param mw: power in mW.
 * @return the next power in mW; 1 W after a power outside the cycle.
 */
uint32_t nextPower(uint32_t mw);

/**
 * Get the label of an FM bandwidth.
 *
 * @param bandwidth: BW_12_5 or BW_25.
 * @return "NFM" or "WFM".
 */
const char *bandwidthLabel(uint8_t bandwidth);

/**
 * Get the label of an operating mode.
 *
 * @param mode: OPMODE_FM, OPMODE_DMR or OPMODE_M17.
 * @return the label.
 */
UiText modeLabel(uint8_t mode);

} // namespace ortxui

#endif /* ORTX_UI_UIFORMAT_HPP */
