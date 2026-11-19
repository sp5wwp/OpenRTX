/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/ContactsView.hpp"

extern "C" {
#include "interfaces/cps_io.h"
}

namespace ortxui
{

ContactsView::ContactsView() :
    CodeplugView(STR(contacts), STR(noContacts), UiText())
{
}

int ContactsView::readName(uint16_t index, char *name) const
{
    contact_t ct;
    const int ret = cps_readContact(&ct, index);
    if (ret == 0)
        copyName(name, ct.name);

    return ret;
}

} // namespace ortxui
