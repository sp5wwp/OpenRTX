/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_FMVIEW_HPP
#define ORTX_UI_FMVIEW_HPP

#include <cstdint>
#include <cstddef>
#include "core/SettingsListView.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The Settings > FM screen in the "value row" style: the current channel's
 * CTCSS tone and its encode/decode enable, each edited by ENTER (value shown
 * as <..>) then UP/right / DOWN/left to cycle, ENTER/ESC to commit. Both edit
 * state.channel.fm directly and request an rtx resync so the change is applied
 * to the radio immediately, mirroring the classic FM settings menu. The base
 * View handles ESC/scroll when not editing.
 */
class FmView : public SettingsListView
{
public:
    void build();
    void syncFromState(const state_t &s) override;

private:
    enum Row : uint8_t {
        RowTone,   //< CTCSS tone frequency (cycles the ctcss_tone table)
        RowEnable, //< None / Encode / Decode / Both
        RowCount,
    };

    void formatValue(uint8_t row, char *out, size_t cap) override;
    void setValueText(uint8_t row, const char *s) override;
    void onAdjust(uint8_t row, int dir) override;

    ListItem items_[RowCount] = {};
    char bufs_[RowCount][12] = {};

    /* Change-gate mirrors of the fields we render. */
    uint8_t lastTone_ = 0xFFu;
    uint8_t lastEnFlags_ = 0xFFu;
};

} // namespace ortxui

#endif /* ORTX_UI_FMVIEW_HPP */
