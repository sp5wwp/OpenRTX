/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/UiFormat.hpp"
#include "rtx/rtx.h"

#include <cstdio>

namespace ortxui
{

/** CTCSS use label, indexed by (txToneEn << 1) | rxToneEn. */
static constexpr UiText TONE_ENABLE_LABELS[4] = {
    STR(None),
    STR(Decode),
    STR(Encode),
    STR(Both),
};

/**
 * Get an FM channel's CTCSS use as a number.
 *
 * @param fm: FM channel settings.
 * @return (txToneEn << 1) | rxToneEn.
 */
static uint8_t toneEnableIndex(const fmInfo_t &fm)
{
    return static_cast<uint8_t>((fm.txToneEn << 1) | fm.rxToneEn);
}

UiText toneEnableLabel(const fmInfo_t &fm)
{
    return TONE_ENABLE_LABELS[toneEnableIndex(fm)];
}

void stepToneEnable(fmInfo_t &fm, int8_t dir)
{
    const uint8_t i = static_cast<uint8_t>((toneEnableIndex(fm) + dir) & 3);
    fm.txToneEn = (i >> 1) & 1u;
    fm.rxToneEn = i & 1u;
}

void stepTone(fmInfo_t &fm, int8_t dir)
{
    const int n = CTCSS_FREQ_NUM;
    const uint8_t t = static_cast<uint8_t>((fm.txTone + dir + n) % n);
    fm.txTone = t;
    fm.rxTone = t;
}

void formatTone(uint8_t index, char *out, size_t n)
{
    const uint16_t t = ctcss_tone[index];
    sniprintf(out, n, "%u.%u", static_cast<unsigned>(t / 10),
              static_cast<unsigned>(t % 10));
}

void formatPower(uint32_t mw, char *out, size_t n)
{
    if (mw >= 1000u)
        sniprintf(out, n, "%lu.%luW", static_cast<unsigned long>(mw / 1000u),
                  static_cast<unsigned long>((mw % 1000u) / 100u));
    else
        sniprintf(out, n, "%lumW", static_cast<unsigned long>(mw));
}

const char *bandwidthLabel(uint8_t bandwidth)
{
    return (bandwidth == BW_25) ? "WFM" : "NFM";
}

UiText modeLabel(uint8_t mode)
{
    switch (mode) {
        case OPMODE_M17:
            return STR(m17);
        case OPMODE_DMR:
            return STR(dmr);
        default:
            return STR(fm);
    }
}

} // namespace ortxui
