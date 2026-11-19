/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/BanksView.hpp"
#include "core/state.h"

extern "C" {
#include "interfaces/cps_io.h"
}

namespace ortxui
{

BanksView::BanksView() : CodeplugView(STR(banks), UiText(), STR(allChannels))
{
}

int BanksView::readName(uint16_t index, char *name) const
{
    bankHdr_t bank;
    const int ret = cps_readBankHeader(&bank, index);
    if (ret == 0)
        copyName(name, bank.name);

    return ret;
}

NavIntent BanksView::pick(uint16_t row)
{
    /* Keep the VFO, so leaving memory mode goes back to it. */
    if (state.tuner_mode == VFO)
        state.vfo_channel = state.channel;

    /* Row 0 is "All channels": channel 0 of the codeplug. */
    int32_t first = 0;
    if (row == 0) {
        state.bank_enabled = false;
    } else {
        const uint16_t bank = static_cast<uint16_t>(row - 1);
        bankHdr_t hdr;
        if (cps_readBankHeader(&hdr, bank) != 0)
            return NavIntent::popToRoot();

        state.bank_enabled = true;
        state.bank = bank;
        first = cps_readBankData(bank, 0);
    }

    channel_t ch;
    if ((first >= 0)
        && (cps_readChannel(&ch, static_cast<uint16_t>(first)) == 0)) {
        state.channel = ch;
        state.channel_index = 0;
        state.tuner_mode = CH;
        requestSyncRtx();
    }

    return NavIntent::popToRoot();
}

} // namespace ortxui
