/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/BanksView.hpp"
#include "core/Event.hpp"
#include "core/state.h"
#include "interfaces/keyboard.h"
#include "hwconfig.h"

extern "C" {
#include "interfaces/cps_io.h"
}

#include <cstring>

namespace ortxui
{

void BanksView::build()
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;

    root_.setArea({ 0, 0, (uint16_t)W, (uint16_t)H });
    root_.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(banks));

    list_.setContentPad(1);
    list_.setFlag(FLAG_FOCUSABLE, true);
    list_.setGrow(1);

    root_.addChild(&bar);
    root_.addChild(&list_);
    screen_.addChild(&root_);

    reload();
}

void BanksView::rowAt(uint16_t index, ListItem &out) const
{
    if (index == 0) {
        out.label = STR(allChannels);
    } else {
        bankHdr_t bank;
        if (cps_readBankHeader(&bank, (uint16_t)(index - 1)) == 0) {
            strncpy(scratch_, bank.name, CPS_STR_SIZE - 1);
            scratch_[CPS_STR_SIZE - 1] = '\0';
        } else {
            scratch_[0] = '\0';
        }
        out.label = scratch_;
    }
    out.value = nullptr;
    out.checkbox = false;
}

void BanksView::reload()
{
    /* Count banks by probing; row 0 ("All channels") is always present, so the
     * list is never empty. */
    bankHdr_t bank;
    bankCount_ = 0;
    while ((bankCount_ < 0xFFFEu)
           && (cps_readBankHeader(&bank, bankCount_) == 0))
        bankCount_++;

    list_.setModel(this);
    list_.setSelected(0);
    root_.onLayout();
    screen_.focusFirst();
    screen_.markAllDirty();
}

void BanksView::onShow()
{
    /* Reload on open — the codeplug may not have been read when build() ran. */
    reload();
}

NavIntent BanksView::onEvent(const Event &e)
{
    if (e.kind == EvKind::Key) {
        if ((e.keys & KEY_ESC) != 0u)
            return NavIntent::pop();

        if ((e.keys & KEY_ENTER) != 0u) {
            /* Preserve the current VFO so an ESC back from memory mode
             * restores it (see VfoView's VFO/MEM toggle). */
            if (state.tuner_mode == VFO)
                state.vfo_channel = state.channel;

            const uint16_t sel = list_.selected();
            channel_t ch;
            bool loaded = false;

            if (sel == 0) {
                /* All channels: clear the bank filter, load global channel 0. */
                state.bank_enabled = false;
                if (cps_readChannel(&ch, 0) == 0) {
                    state.channel = ch;
                    state.channel_index = 0;
                    state.tuner_mode = CH;
                    loaded = true;
                }
            } else {
                const uint16_t bankPos = (uint16_t)(sel - 1);
                bankHdr_t bank;
                if (cps_readBankHeader(&bank, bankPos) == 0) {
                    state.bank_enabled = true;
                    state.bank = bankPos;
                    /* Load the bank's first channel (its global index). */
                    const int g = cps_readBankData(bankPos, 0);
                    if ((g >= 0) && (cps_readChannel(&ch, (uint16_t)g) == 0)) {
                        state.channel = ch;
                        state.channel_index = 0;
                        state.tuner_mode = CH;
                        loaded = true;
                    }
                }
            }

            if (loaded)
                requestSyncRtx();
            return NavIntent::popToRoot();
        }
    }

    screen_.dispatch(e);
    return NavIntent::none();
}

} // namespace ortxui
