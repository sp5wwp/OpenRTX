/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_BANKSVIEW_HPP
#define ORTX_UI_BANKSVIEW_HPP

#include <cstdint>
#include "views/CodeplugView.hpp"

namespace ortxui
{

/**
 * The main-menu bank browser: an "All channels" row, then the codeplug's
 * banks. ENTER on "All channels" leaves the bank, on a bank enables it; both
 * load the first channel and go back to the home screen in memory mode.
 */
class BanksView : public CodeplugView
{
public:
    /** Constructor. */
    BanksView();

    /** Destructor. */
    ~BanksView() = default;

private:
    /** Read a bank's name. */
    int readName(uint16_t index, char *name) const override;

    /** Enable the bank on the row, or none, and load its first channel. */
    NavIntent pick(uint16_t row) override;
};

} // namespace ortxui

#endif /* ORTX_UI_BANKSVIEW_HPP */
