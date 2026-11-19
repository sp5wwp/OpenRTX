/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/FmView.hpp"
#include "core/cps.h"

#include <cstdio>

namespace ortxui
{

/* Encode/decode enable label, indexed by (rxToneEn << 1) | txToneEn: the TX
 * tone encodes, the RX tone decodes. */
static constexpr UiText ENABLE_LABELS[4] = { STR(None), STR(Encode),
                                             STR(Decode), STR(Both) };

void FmView::build()
{
    items_[RowTone].label = STR(CTCSSTone);
    items_[RowEnable].label = STR(CTCSSEn);
    buildList(STR(fm), items_.data(), RowCount);
}

void FmView::formatValue(uint8_t row, char *out, size_t cap)
{
    const fmInfo_t &fm = state.channel.fm;

    if (row == RowTone) {
        const uint16_t t = ctcss_tone[fm.txTone];
        sniprintf(out, cap, "%u.%u Hz", (unsigned)(t / 10), (unsigned)(t % 10));
    } else {
        const uint8_t idx = (uint8_t)((fm.rxToneEn << 1) | fm.txToneEn);
        sniprintf(out, cap, "%s", ENABLE_LABELS[idx].str());
    }
}

void FmView::onAdjust(uint8_t row, int8_t dir)
{
    fmInfo_t &fm = state.channel.fm;

    if (row == RowTone) {
        const int n = CTCSS_FREQ_NUM;
        int t = (int)fm.txTone + dir;
        t = ((t % n) + n) % n;  /* wrap both directions */
        fm.txTone = (uint8_t)t;
        fm.rxTone = (uint8_t)t; /* the RX tone follows the TX tone */
    } else {
        /* Step (tx << 1 | rx): None, Decode, Encode, Both. */
        int flags = (fm.txToneEn << 1) | fm.rxToneEn;
        flags = ((flags + dir) & 3);
        fm.txToneEn = (uint8_t)((flags >> 1) & 1);
        fm.rxToneEn = (uint8_t)(flags & 1);
    }

    requestSyncRtx(); /* re-tune the radio with the new tone/enable */
}

} // namespace ortxui
