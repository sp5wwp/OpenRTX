/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_LISTMODEL_HPP
#define ORTX_UI_LISTMODEL_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>
#include "base/UiText.hpp"

namespace ortxui
{

/**
 * A single row of a List, shaped by the fields set: a label only, a label and
 * a value, or a label and a checkbox. Plain texts are borrowed; the owner
 * keeps them (and the item array) alive.
 */
struct ListItem {
    UiText label;          ///< Row label
    UiText value;          ///< Value text; left unset for a value-less row
    bool checkbox = false; ///< Row is a checkbox
    bool checked = false;  ///< Checkbox state
};

/**
 * An on-demand source of list rows: the List reads only the rows it shows, so
 * a large list such as a codeplug's channels needs no per-row storage.
 * rowAt() may point the row's texts at a buffer valid until its next call.
 */
class ListModel
{
public:
    /** Constructor. */
    ListModel() = default;

    /** Destructor. */
    virtual ~ListModel() = default;

    /**
     * Get the number of rows.
     *
     * @return the number of rows.
     */
    virtual uint16_t rowCount() const = 0;

    /**
     * Fill a row.
     *
     * @param index: row index.
     * @param out: the row, default-constructed by the List.
     */
    virtual void rowAt(uint16_t index, ListItem &out) const = 0;

    /**
     * Check whether every row is a plain label: the rows then share one
     * height, so the List places them without reading them.
     *
     * @return true if every row is a plain label.
     */
    virtual bool plainRows() const
    {
        return false;
    }
};

} // namespace ortxui

#endif /* ORTX_UI_LISTMODEL_HPP */
