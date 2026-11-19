/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_LIST_HPP
#define ORTX_UI_LIST_HPP

#include <cstdint>
#include "core/Object.hpp"
#include "core/UiText.hpp"
#include "core/graphics.h"
#include "style/SemanticColor.hpp"
#include "widgets/ListModel.hpp"
#include "widgets/SettingRow.hpp"

namespace ortxui
{

/**
 * Selection and scroll offset of a list, kept while its view is destroyed.
 */
struct ListPos {
    uint8_t selected; ///< Selected row
    uint8_t top;      ///< First visible row
};

/**
 * A vertical, single-selection list of rows (mockups 2-4).
 *
 * The whole list is one focusable Object owning the selected index and scroll
 * offset, so a menu of any length stays within the Screen focus ring and maps
 * to the rotary knob / arrow keys. Encoder steps and UP/DOWN move the selection
 * (wrapping) and scroll to keep it visible; ENTER/ESC are left for the owning
 * view (which reads selected()/item() to navigate or toggle). A list without a
 * selection scrolls by a row instead, stopping at either end. The selected row
 * is a solid blue fill with black text; rows are separated by thin dividers.
 *
 * The item table is borrowed, not copied, matching the no-heap discipline.
 */
class List : public Object
{
public:
    /** Constructor. */
    List() = default;

    /** Destructor. */
    ~List() = default;

    /** Back the list with a borrowed static item array (small, fixed menus). */
    void setItems(const ListItem *items, uint16_t count);
    /** Back the list with an on-demand model (large / lazily-read lists). */
    void setModel(const ListModel *model);
    /** Per-row vertical content inset and label/value gutter. Rows size
     *  themselves to their content plus this inset, so a value row is taller
     *  than a plain/checkbox row but every row keeps the same padding. */
    void setContentPad(int16_t padY, int16_t gap = 2)
    {
        padY_ = padY;
        gap_ = gap;
    }
    void setSelectable(bool s)
    {
        selectable_ = s;
    }

    uint16_t selected() const
    {
        return selected_;
    }
    /** Select row `i`; `announce` false keeps a background re-sync silent. */
    void setSelected(uint16_t i, bool announce = true);

    /**
     * Get the selection and scroll offset.
     *
     * @return the position.
     */
    ListPos position() const;

    /**
     * Put back a position from position(), silently.
     *
     * @param p: position.
     */
    void setPosition(const ListPos &p);

    /** Mutable access to the selected item (e.g. to toggle a checkbox). */
    ListItem *selectedItem();

    /** Speak the current row via voice prompts (no-op unless vpLevel >= vpLow).
     * Called automatically on selection change; views call it on entry too. */
    void announceSelection() const;

    bool onEvent(const Event &e) override;
    void draw(DrawCtx &d) override;

private:
    void scrollToSelected();
    void moveSelection(int dir);

    /**
     * Move the window of a list without a selection by a row, keeping the
     * last page full.
     *
     * @param dir: +1 down, -1 up.
     */
    void scroll(int dir);

    /**
     * Get the scroll offset that shows the last row at the bottom.
     *
     * @return the largest first visible row.
     */
    uint16_t maxTop() const;

    void getItem(uint16_t i, ListItem &out) const;
    static SettingRow::Kind kindOf(const ListItem &it);
    int16_t rowHeight(uint16_t i) const;

    /** Effective row count from whichever backing (model or static array). */
    uint16_t rows() const
    {
        return (model_ != nullptr) ? model_->rowCount() : count_;
    }

    const ListItem *items_ = nullptr;
    const ListModel *model_ = nullptr;
    uint16_t count_ = 0;
    uint16_t selected_ = 0;
    uint16_t top_ = 0; //< index of the first visible row (scroll offset)
    int16_t padY_ = 4; //< per-row vertical content inset
    int16_t gap_ = 2;  //< label/value gutter in a value row
    bool selectable_ = true;
};

} // namespace ortxui

#endif /* ORTX_UI_LIST_HPP */
