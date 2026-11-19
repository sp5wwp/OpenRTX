/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/ChecklistView.hpp"

namespace ortxui
{

void ChecklistView::build()
{
    const settings_t &st = state.settings;

    items_[RowPhonetic].label = STR(phonetic);
    items_[RowPhonetic].checked = (st.vpPhoneticSpell != 0u);
    items_[RowLatch].label = STR(macroLatching);
    items_[RowLatch].checked = (st.macroMenuLatch != 0u);
    for (ListItem &it : items_)
        it.checkbox = true;

    buildList(STR(accessibility), items_.data(), RowCount);
}

void ChecklistView::onToggle(uint8_t row, bool on)
{
    /* Settings reach flash at shutdown. */
    if (row == RowPhonetic)
        state.settings.vpPhoneticSpell = on ? 1u : 0u;
    else
        state.settings.macroMenuLatch = on ? 1u : 0u;
}

} // namespace ortxui
