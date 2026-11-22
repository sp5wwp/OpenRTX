/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "core/utils.h"
#include <stdio.h>
#include <math.h>
#include "core/cps.h"
#include "interfaces/platform.h"

/** FNV-1a multiplier for 32-bit hashes. */
static const uint32_t FNV1A_PRIME = 16777619u;

uint8_t interpCalParameter(const freq_t freq, const freq_t *calPoints,
                           const uint8_t *param, const uint8_t elems)
{

    if(freq <= calPoints[0])         return param[0];
    if(freq >= calPoints[elems - 1]) return param[elems - 1];

    /* Find calibration point nearest to target frequency */
    uint8_t pos = 0;
    for(; pos < elems; pos++)
    {
        if(calPoints[pos] >= freq) break;
    }

    uint8_t interpValue = 0;
    freq_t  delta = calPoints[pos] - calPoints[pos - 1];

    if(param[pos - 1] < param[pos])
    {
        interpValue = param[pos - 1] + ((freq - calPoints[pos - 1]) *
                                        (param[pos] - param[pos - 1]))/delta;
    }
    else
    {
        interpValue = param[pos - 1] - ((freq - calPoints[pos - 1]) *
                                       (param[pos - 1] - param[pos]))/delta;
    }

    return interpValue;
}

uint32_t bcdToBin(uint32_t bcd)
{
    return ((bcd >> 28) & 0x0F) * 10000000 +
           ((bcd >> 24) & 0x0F) * 1000000 +
           ((bcd >> 20) & 0x0F) * 100000 +
           ((bcd >> 16) & 0x0F) * 10000 +
           ((bcd >> 12) & 0x0F) * 1000 +
           ((bcd >> 8) & 0x0F)  * 100 +
           ((bcd >> 4) & 0x0F)  * 10 +
           (bcd & 0x0F);
}

void stripTrailingZeroes(char *str)
{
    for(size_t i = strlen(str); i > 2; i--)
    {
        if((str[i - 1] != '0') || (str[i - 2] == '.'))
        {
            str[i] = '\0';
            return;
        }
    }
}

uint8_t rssiToSlevel(const rssi_t rssi)
{
    // RSSI >= -53dB is S11 (S9 + 20 dB)
    if(rssi >= -53)
        return 11;

    // RSSI lower than -121dB is always S0
    if(rssi < -121)
        return 0;

    // For S level > 9 use 10dB steps instead of 6dB
    if (rssi >= -73)
        return (uint8_t)((163 + rssi) / 10);

    // For S1 - S9 use 6dB increase per S-Point
    return (uint8_t)(127 + rssi) / 6;
}

rssi_t squelchToRssi(const uint8_t level)
{
    // Squelch 0 to 15 spans -127 dBm to -61 dBm
    return -127 + (level * 66) / 15;
}

uint8_t ctcssFreqToIndex(const uint16_t freq)
{
    for(uint8_t idx = 0; idx < CTCSS_FREQ_NUM; idx += 1)
    {
        if(ctcss_tone[idx] == freq)
            return idx;
    }

    return 255;
}

size_t utf8Encode(uint32_t cp, char *out)
{
    if (cp < 0x80u) {
        out[0] = (char)cp;
        return 1;
    }

    if (cp < 0x800u) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }

    out[0] = (char)(0xE0u | (cp >> 12));
    out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[2] = (char)(0x80u | (cp & 0x3Fu));
    return 3;
}

uint32_t fnv1a(const uint32_t hash, const uint32_t value)
{
    return (hash ^ value) * FNV1A_PRIME;
}

#ifdef CONFIG_RF_BANDS_MHZ
/** A band of the radio: its first and last frequency, in MHz. */
struct rfBand
{
    uint16_t minFreq;
    uint16_t maxFreq;
};

static const struct rfBand rfBands[] = { CONFIG_RF_BANDS_MHZ };
#endif

int8_t freqBand(const freq_t freq)
{
#ifdef CONFIG_RF_BANDS_MHZ
    for(size_t i = 0; i < sizeof(rfBands) / sizeof(rfBands[0]); i++)
    {
        if((freq >= (freq_t) rfBands[i].minFreq * 1000000u)
        && (freq <= (freq_t) rfBands[i].maxFreq * 1000000u))
            return (int8_t) i;
    }
#else
    // hwInfo_t band limits are in MHz
    const hwInfo_t *hw = platform_getHwInfo();

    if(hw->vhf_band && (freq >= (freq_t) hw->vhf_minFreq * 1000000u)
                    && (freq <= (freq_t) hw->vhf_maxFreq * 1000000u))
        return 0;

    if(hw->uhf_band && (freq >= (freq_t) hw->uhf_minFreq * 1000000u)
                    && (freq <= (freq_t) hw->uhf_maxFreq * 1000000u))
        return 1;
#endif

    return -1;
}

bool freqInBand(const freq_t freq)
{
    return freqBand(freq) >= 0;
}
