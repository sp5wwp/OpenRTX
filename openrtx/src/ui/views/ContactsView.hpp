/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_CONTACTSVIEW_HPP
#define ORTX_UI_CONTACTSVIEW_HPP

#include <cstdint>
#include "views/CodeplugView.hpp"

namespace ortxui
{

/**
 * The main-menu contact browser, read only: ENTER does nothing.
 */
class ContactsView : public CodeplugView
{
public:
    /** Constructor. */
    ContactsView();

    /** Destructor. */
    ~ContactsView() = default;

private:
    /** Read a contact's name. */
    int readName(uint16_t index, char *name) const override;
};

} // namespace ortxui

#endif /* ORTX_UI_CONTACTSVIEW_HPP */
