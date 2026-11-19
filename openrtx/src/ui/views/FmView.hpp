/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_FMVIEW_HPP
#define ORTX_UI_FMVIEW_HPP

#include <array>
#include <cstdint>
#include <cstddef>
#include "views/SettingsListView.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The Settings > FM screen: the current channel's CTCSS tone and whether it
 * is encoded, decoded or both. Both edit state.channel.fm and reconfigure the
 * radio at once.
 */
class FmView : public SettingsListView
{
public:
    /** Constructor. */
    FmView() = default;

    /** Destructor. */
    ~FmView() = default;

    /** Build the rows from the current channel. */
    void build() override;

private:
    /** Render a row's value. */
    void formatValue(uint8_t row, char *out, size_t cap) override;

    /** Step the tone or the tone enable. */
    void onAdjust(uint8_t row, int8_t dir) override;

    /** Speak the tones through the CTCSS voice prompt helper. */
    void announceValue(uint8_t row, const UiText &value,
                       vpQueueFlags flags) override;

    /** Rows, top to bottom. */
    enum Row : uint8_t {
        RowTone,   ///< CTCSS tone frequency
        RowEnable, ///< None, Encode, Decode or Both
        RowCount,
    };

    std::array<ListItem, RowCount> items_ = {}; ///< Rows
};

} // namespace ortxui

#endif /* ORTX_UI_FMVIEW_HPP */
