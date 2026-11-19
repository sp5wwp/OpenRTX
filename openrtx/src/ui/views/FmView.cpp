/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/FmView.hpp"
#include "core/Event.hpp"
#include "core/cps.h"
#include "interfaces/keyboard.h"
#include "hwconfig.h"

#include <cstdio>

namespace ortxui
{

/* Encode/decode enable label, indexed by (rxToneEn << 1) | txToneEn — the same
 * order the classic UI uses (tx tone = encode, rx tone = decode). */
static const UiText kEnableLabels[4] = { STR(None), STR(Encode), STR(Decode),
                                         STR(Both) };

void FmView::build()
{
    static const UiText kLabels[RowCount] = {
        STR(CTCSSTone),
        STR(CTCSSEn),
    };

    for (uint8_t i = 0; i < RowCount; i++) {
        items_[i].label = kLabels[i];
        items_[i].value = bufs_[i];
    }
    buildList(STR(fm), items_, RowCount);
    for (uint8_t i = 0; i < RowCount; i++)
        refreshValue(i);

    lastTone_ = state.channel.fm.txTone;
    lastEnFlags_ = (uint8_t)((state.channel.fm.txToneEn << 1)
                             | state.channel.fm.rxToneEn);
}

void FmView::formatValue(uint8_t row, char *out, size_t cap)
{
    const fmInfo_t &fm = state.channel.fm; /* live source of truth */

    if (row == RowTone) {
        const uint16_t t = ctcss_tone[fm.txTone];
        sniprintf(out, cap, "%u.%u Hz", (unsigned)(t / 10), (unsigned)(t % 10));
    } else {
        const uint8_t idx = (uint8_t)((fm.rxToneEn << 1) | fm.txToneEn);
        sniprintf(out, cap, "%s", kEnableLabels[idx].str());
    }
}

void FmView::setValueText(uint8_t row, const char *s)
{
    sniprintf(bufs_[row], sizeof(bufs_[row]), "%s", s);
}

void FmView::onAdjust(uint8_t row, int dir)
{
    fmInfo_t &fm = state.channel.fm;

    if (row == RowTone) {
        const int n = CTCSS_FREQ_NUM;
        int t = (int)fm.txTone + dir;
        t = ((t % n) + n) % n;  /* wrap both directions */
        fm.txTone = (uint8_t)t;
        fm.rxTone = (uint8_t)t; /* keep RX in step with TX, as classic does */
        lastTone_ = (uint8_t)t;
    } else {
        /* Cycle None -> Encode -> Decode -> Both. Classic keys the cycle on
         * (tx << 1 | rx), distinct from the display order. */
        int flags = (fm.txToneEn << 1) | fm.rxToneEn;
        flags = ((flags + dir) & 3);
        fm.txToneEn = (uint8_t)((flags >> 1) & 1);
        fm.rxToneEn = (uint8_t)(flags & 1);
        lastEnFlags_ = (uint8_t)((fm.txToneEn << 1) | fm.rxToneEn);
    }

    requestSyncRtx(); /* re-tune the radio with the new tone/enable */
}

void FmView::syncFromState(const state_t &s)
{
    View::syncFromState(s); /* top bar */

    const fmInfo_t &fm = s.channel.fm;
    const uint8_t tone = fm.txTone;
    const uint8_t enFlags = (uint8_t)((fm.txToneEn << 1) | fm.rxToneEn);
    bool changed = false;

    if (!(editing_ && editRow_ == RowTone) && (tone != lastTone_)) {
        refreshValue(RowTone);
        lastTone_ = tone;
        changed = true;
    }
    if (!(editing_ && editRow_ == RowEnable) && (enFlags != lastEnFlags_)) {
        refreshValue(RowEnable);
        lastEnFlags_ = enFlags;
        changed = true;
    }

    if (changed)
        list_.invalidate();
}

} // namespace ortxui
