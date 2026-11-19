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
    uint16_t selected; ///< Selected row
    uint16_t top;      ///< First visible row
};

/**
 * A vertical, single-selection list of rows.
 *
 * The whole list is one focusable Object owning the selected index and scroll
 * offset, so a menu of any length stays within the Screen focus ring. The knob
 * and UP/DOWN move the selection, wrapping, and scroll to keep it visible;
 * ENTER and ESC are left to the owning view. A list without a selection
 * scrolls by a row instead, stopping at either end. Rows are separated by thin
 * dividers and the selected row is filled with the primary colour.
 *
 * The item table or model is borrowed, not copied.
 */
class List : public Object
{
public:
    /** Constructor. */
    List() = default;

    /** Destructor. */
    ~List() = default;

    /**
     * Show a fixed table of rows, from the top.
     *
     * @param items: rows, kept alive by the caller.
     * @param count: number of rows.
     */
    void setItems(const ListItem *items, uint16_t count);

    /**
     * Show the rows of a model, read on demand, from the top.
     *
     * @param model: model, kept alive by the caller.
     */
    void setModel(const ListModel *model);

    /**
     * Set the vertical inset of each row's content; rows size themselves to
     * their content plus this inset.
     *
     * @param padY: inset above and below, in pixels.
     */
    void setContentPad(int16_t padY)
    {
        padY_ = padY;
    }

    /**
     * Set whether the list has a selection, or only scrolls.
     *
     * @param s: true for a selection.
     */
    void setSelectable(bool s)
    {
        selectable_ = s;
    }

    /** Get the selected row. */
    uint16_t selected() const
    {
        return selected_;
    }

    /**
     * Select a row and scroll it into view.
     *
     * @param i: row, clamped to the last one.
     * @param announce: false keeps a background re-sync silent.
     */
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

    /**
     * Get the selected row of a fixed table, to change it in place.
     *
     * @return the row, or nullptr for a model.
     */
    ListItem *selectedItem();

    /**
     * Move the selection, or the window of a list without one.
     *
     * @param e: event.
     * @return true for a knob step or an UP/DOWN press.
     */
    bool onEvent(const Event &e) override;

    /**
     * Paint the rows in the window and the scroll bar.
     *
     * @param d: drawing context.
     */
    void draw(DrawCtx &d) override;

    /**
     * Paint a scroll bar at the right edge of an area: a track, with a thumb
     * for the part shown.
     *
     * @param d: drawing context.
     * @param area: area scrolled.
     * @param offset: start of the part shown.
     * @param shown: length of the part shown.
     * @param total: total length, in the same unit.
     */
    static void drawScrollBar(DrawCtx &d, const Rect &area, int32_t offset,
                              int32_t shown, int32_t total);

private:
    /** Pull the window to the selection, reading at most one window of rows. */
    void scrollToSelected();

    /**
     * Move the selection by a row, wrapping at either end.
     *
     * @param dir: +1 down, -1 up.
     */
    void moveSelection(int8_t dir);

    /**
     * Move the window of a list without a selection by a row, keeping the
     * last page full.
     *
     * @param dir: +1 down, -1 up.
     */
    void scroll(int8_t dir);

    /**
     * Get the scroll offset that shows the last row at the bottom.
     *
     * @return the largest first visible row.
     */
    uint16_t maxTop() const;

    /**
     * Check whether the model says every row is a plain label.
     *
     * @return true if rows can be placed without reading them.
     */
    bool plainRows() const;

    /**
     * Read a row from the table or the model.
     *
     * @param i: row.
     * @param out: the row.
     */
    void getItem(uint16_t i, ListItem &out) const;

    /**
     * Get the shape of a row.
     *
     * @param it: row.
     * @return the row kind.
     */
    static SettingRow::Kind kindOf(const ListItem &it);

    /**
     * Get the height of a row, without reading it for plain rows.
     *
     * @param i: row.
     * @return the height in pixels.
     */
    int16_t rowHeight(uint16_t i) const;

    /** Speak the selected row, when voice prompts are on. */
    void announceSelection() const;

    /** Get the number of rows of the table or the model. */
    uint16_t rows() const
    {
        return (model_ != nullptr) ? model_->rowCount() : count_;
    }

    /** Space between a row's label and its value line. */
    static constexpr int16_t VALUE_GAP = 2;

    /** Width of the scroll bar, and the shortest thumb. */
    static constexpr uint16_t SCROLL_BAR_W = 2;

    const ListItem *items_ = nullptr;  ///< Fixed table, or nullptr
    const ListModel *model_ = nullptr; ///< Model, or nullptr
    uint16_t count_ = 0;               ///< Rows of the fixed table
    uint16_t selected_ = 0;            ///< Selected row
    uint16_t top_ = 0;                 ///< First visible row
    int16_t padY_ = 4;                 ///< Vertical inset of each row
    bool selectable_ = true;           ///< The list has a selection
};

} // namespace ortxui

#endif /* ORTX_UI_LIST_HPP */
