/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/FmView.hpp"
#include "base/UiFormat.hpp"
#include "core/voicePromptUtils.h"

#include <cstdio>

namespace ortxui
{

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
        char tone[8];
        formatTone(fm.txTone, tone, sizeof(tone));
        sniprintf(out, cap, "%s Hz", tone);
    } else {
        sniprintf(out, cap, "%s", toneEnableLabel(fm).str());
    }
}

void FmView::onAdjust(uint8_t row, int8_t dir)
{
    fmInfo_t &fm = state.channel.fm;

    if (row == RowTone)
        stepTone(fm, dir);
    else
        stepToneEnable(fm, dir);

    requestSyncRtx(); /* re-tune the radio with the new tone/enable */
}

void FmView::announceValue(uint8_t row, const UiText &value, vpQueueFlags flags)
{
    (void)value;
    const fmInfo_t &fm = state.channel.fm;

    /* The tone row speaks the tone whether or not it is in use. */
    if (row == RowTone)
        vp_announceCTCSS(true, fm.rxTone, true, fm.txTone, flags);
    else
        vp_announceCTCSS(fm.rxToneEn, fm.rxTone, fm.txToneEn, fm.txTone, flags);
}

} // namespace ortxui
