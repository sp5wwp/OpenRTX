/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_CONTACTSVIEW_HPP
#define ORTX_UI_CONTACTSVIEW_HPP

#include <cstdint>
#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Widgets.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/List.hpp"
#include "core/cps.h"

namespace ortxui
{

/**
 * The main-menu Contacts browser: a scrollable list of the codeplug's contact
 * names (read on entry via cps_readContact). A read-only viewer, mirroring the
 * classic Contacts menu — ENTER has no action, ESC goes back. When the codeplug
 * has no contacts a centred "No contacts" line is shown instead.
 *
 * Rows are read on demand through a ListModel (see ChannelsView), so contact
 * names are never all held in RAM.
 */
class ContactsView : public View, public ListModel
{
public:
    void build();
    void onShow() override;
    NavIntent onEvent(const Event &e) override;

    Screen &screen() override
    {
        return screen_;
    }

    /* ListModel: the contact names, read lazily from the codeplug. */
    uint16_t rowCount() const override
    {
        return count_;
    }
    void rowAt(uint16_t index, ListItem &out) const override;

private:
    void reload();

    Screen screen_;
    Flex root_;
    TopBar topBar_;
    List list_;
    Label empty_;

    uint16_t count_ = 0;
    mutable char scratch_[CPS_STR_SIZE] = {};
};

} // namespace ortxui

#endif /* ORTX_UI_CONTACTSVIEW_HPP */
