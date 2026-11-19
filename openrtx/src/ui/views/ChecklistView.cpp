/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/ChecklistView.hpp"
#include "core/voicePromptUtils.h"

#include <cstdio>

namespace ortxui
{

void ChecklistView::build()
{
    const settings_t &st = state.settings;

    items_[RowLatch].label = STR(macroLatching);
    items_[RowLatch].checkbox = true;
    items_[RowLatch].checked = (st.macroMenuLatch != 0u);
    items_[RowVoice].label = STR(voice);
    items_[RowPhonetic].label = STR(phonetic);
    items_[RowPhonetic].checkbox = true;
    items_[RowPhonetic].checked = (st.vpPhoneticSpell != 0u);

    buildList(STR(accessibility), items_.data(), RowCount);
}

ChecklistView::RowKind ChecklistView::rowKind(uint8_t row) const
{
    return (row == RowVoice) ? RowKind::Stepper : RowKind::Checkbox;
}

void ChecklistView::formatValue(uint8_t row, char *out, size_t cap)
{
    (void)row;
    const uint8_t level = state.settings.vpLevel;
    if (level == vpNone)
        sniprintf(out, cap, "%s", STR(off).str());
    else if (level == vpBeep)
        sniprintf(out, cap, "%s", STR(beep).str());
    else
        sniprintf(out, cap, "%u", static_cast<unsigned>(level - vpBeep));
}

void ChecklistView::onAdjust(uint8_t row, int8_t dir)
{
    (void)row;
    const int level = state.settings.vpLevel + dir;
    if ((level >= vpNone) && (level <= vpHigh))
        state.settings.vpLevel = static_cast<uint8_t>(level);
}

void ChecklistView::onToggle(uint8_t row, bool on)
{
    /* Settings reach flash at shutdown. */
    if (row == RowPhonetic)
        state.settings.vpPhoneticSpell = on ? 1u : 0u;
    else
        state.settings.macroMenuLatch = on ? 1u : 0u;
}

void ChecklistView::announceValue(uint8_t row, const UiText &value,
                                  vpQueueFlags flags)
{
    if (row != RowVoice) {
        SettingsListView::announceValue(row, value, flags);
        return;
    }

    /* Play the new level at once, even the lowest, so it can be heard. */
    flags = static_cast<vpQueueFlags>(vpqInit | vpqAddSeparatingSilence
                                      | vpqPlayImmediately);
    if (!vp_isPlaying())
        flags = static_cast<vpQueueFlags>(flags | vpqIncludeDescriptions);
    vp_announceSettingsVoiceLevel(flags);
}

} // namespace ortxui
