/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_LISTMODEL_HPP
#define ORTX_UI_LISTMODEL_HPP

#include <cstdint>
#include "core/UiText.hpp"

namespace ortxui
{

/**
 * A single row of a List. The row style follows the fields set:
 *  - label only            -> a plain row (submenu / credits),
 *  - label + value         -> a settings row (label top-left, value low-right),
 *  - label + checkbox      -> a checklist row (box + tick when checked).
 * Plain texts are borrowed; the owner keeps them (and the item array) alive.
 */
struct ListItem {
    UiText label;          ///< Row label
    UiText value;          ///< Value text; left unset for a value-less row
    bool checkbox = false; ///< Row is a checkbox
    bool checked = false;  ///< Checkbox state
};

/**
 * An on-demand source of list rows. Only the currently visible window is
 * queried (during draw), so a large list — e.g. a codeplug's hundreds of
 * channels — needs no per-row storage: the model reads each row lazily.
 *
 * rowAt() may point out.label at a scratch buffer that is only valid until the
 * next rowAt() call; the List draws each row immediately after fetching it.
 */
class ListModel
{
public:
    /** Constructor. */
    ListModel() = default;

    virtual ~ListModel() = default;
    virtual uint16_t rowCount() const = 0;
    virtual void rowAt(uint16_t index, ListItem &out) const = 0;
};

} // namespace ortxui

#endif /* ORTX_UI_LISTMODEL_HPP */
