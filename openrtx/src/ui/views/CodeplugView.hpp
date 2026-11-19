/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_CODEPLUGVIEW_HPP
#define ORTX_UI_CODEPLUGVIEW_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>
#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Label.hpp"
#include "widgets/List.hpp"
#include "core/cps.h"

namespace ortxui
{

/**
 * A browser of codeplug entries: their names in a list, or a line saying
 * there are none. The entries are counted once when the browser opens and
 * their names read on demand, so only the rows shown hit the codeplug.
 * Subclasses read the names and act on ENTER; ESC goes back.
 */
class CodeplugView : public View, public ListModel
{
public:
    /**
     * Constructor.
     *
     * @param title: screen title.
     * @param none: line shown when there are no rows.
     * @param first: fixed row above the entries, or an unset text for none.
     */
    CodeplugView(const UiText &title, const UiText &none, const UiText &first);

    /** Destructor. */
    ~CodeplugView() = default;

    /** Build the widget tree and count the entries. */
    void build() override;

    /** Act on ENTER, go back on ESC, move the selection otherwise. */
    NavIntent onEvent(const Event &e) override;

    /** The view's widget-tree root. */
    Screen &screen() override
    {
        return screen_;
    }

    /** The list, whose position UiMemory keeps. */
    List *list() override
    {
        return &list_;
    }

    /** Get the number of rows: the fixed row, if any, and the entries. */
    uint16_t rowCount() const override;

    /** Fill a row with the fixed row or an entry's name. */
    void rowAt(uint16_t index, ListItem &out) const override;

    /** Every row is a name. */
    bool plainRows() const override
    {
        return true;
    }

protected:
    /**
     * Read the name of an entry.
     *
     * @param index: entry index.
     * @param name: CPS_STR_SIZE bytes the name is copied to.
     * @return 0 if the entry exists.
     */
    virtual int readName(uint16_t index, char *name) const = 0;

    /**
     * Act on ENTER over a row; the default does nothing.
     *
     * @param row: selected row, the fixed row being row 0.
     * @return what the navigator does next.
     */
    virtual NavIntent pick(uint16_t row);

    /**
     * Copy a codeplug name, which need not end in a NUL.
     *
     * @param dst: CPS_STR_SIZE bytes, always terminated.
     * @param src: name field of a codeplug entry.
     */
    static void copyName(char *dst, const char *src);

private:
    UiText title_;                         ///< Screen title
    UiText none_;                          ///< Text of the empty state
    UiText first_;                         ///< Fixed first row, unset if none
    Screen screen_;                        ///< Widget-tree root
    Flex root_;                            ///< Column: top bar over the list
    List list_;                            ///< The rows
    Label empty_;                          ///< Shown instead of an empty list
    uint16_t entries_ = 0;                 ///< Number of codeplug entries
    mutable char name_[CPS_STR_SIZE] = {}; ///< Name of the last row read
};

} // namespace ortxui

#endif /* ORTX_UI_CODEPLUGVIEW_HPP */
