/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_SETTINGROW_HPP
#define ORTX_UI_SETTINGROW_HPP

#include <cstdint>
#include "layout/Flex.hpp"
#include "widgets/Label.hpp"

namespace ortxui
{

/**
 * One List row: a label, with a value beside it or, if they do not fit side
 * by side, under it, or with a checkbox. A List binds one SettingRow to each
 * row in turn, lays it out and paints it; every kind keeps the same padding,
 * so rows differ only in height. The row paints its selection fill, separator
 * and checkbox; the label and value paint after it.
 */
class SettingRow : public Flex
{
public:
    /** How a row shows its item. */
    enum class Kind : uint8_t {
        Plain,    ///< A label only
        Value,    ///< A label and a value, on one line if they fit
        Checkbox, ///< A label and a checkbox at the right
    };

    /** Constructor. */
    SettingRow();

    /** Destructor. */
    ~SettingRow() = default;

    /** Per-list geometry: vertical content inset and the label/value gutter. */
    void setPad(int16_t padY, int16_t gap);

    /**
     * Bind the flyweight to one row's content and selection state.
     *
     * @param kind: row kind.
     * @param label: row label.
     * @param value: value text, used by a value row only.
     * @param checked: checkbox state.
     * @param selected: whether the row is selected.
     */
    void bind(Kind kind, const UiText &label, const UiText &value, bool checked,
              bool selected);

    /**
     * Row height for a given kind and content at the given row width, so the
     * List can lay rows out before binding them. A value row that fits side
     * by side is as short as a plain row; otherwise it carries a second line.
     *
     * @param kind: row kind.
     * @param label: row label, used by a value row only.
     * @param value: value text, used by a value row only.
     * @param rowW: row width.
     * @return the row height.
     */
    int16_t heightFor(Kind kind, const UiText &label, const UiText &value,
                      int16_t rowW) const;

    /** The row's width by its height for the bound text. */
    Size natural() const override;

    /** Lay the label and value on one line or two. */
    void onLayout() override;

    /** Paint the selection fill, the separator and the checkbox. */
    void draw(DrawCtx &d) override;

private:
    /**
     * Check whether a value row's label and value fit side by side on one
     * line, with a minimum gutter between them.
     *
     * @param label: row label.
     * @param value: value text.
     * @param rowW: row width.
     * @return true if they fit.
     */
    bool fitsOneLine(const UiText &label, const UiText &value,
                     int16_t rowW) const;

    static constexpr int16_t PAD_X = 6;     ///< Inset of the label and value
    static constexpr int16_t FIT_GAP = 8;   ///< Least gap on a shared line
    static constexpr int16_t BOX = 12;      ///< Checkbox size
    static constexpr int16_t BOX_INSET = 4; ///< Checkbox inset from the right

    /** Label font; smaller on the compact screen to keep the list dense. */
    static constexpr fontSize_t LABEL_FONT =
        (sizeClass() == SizeClass::Regular) ? FONT_SIZE_8PT : FONT_SIZE_6PT;

    /** Value font. */
    static constexpr fontSize_t VALUE_FONT =
        (sizeClass() == SizeClass::Regular) ? FONT_SIZE_6PT : FONT_SIZE_5PT;

    Label label_;             ///< Row label
    Label value_;             ///< Row value, for a value row
    Kind kind_ = Kind::Plain; ///< Row kind
    bool checked_ = false;    ///< Checkbox state
    bool selected_ = false;   ///< The row is selected
    int16_t padY_ = 4;        ///< Vertical inset
    int16_t gap_ = 2;         ///< Gap between the label and a value below it
};

} // namespace ortxui

#endif /* ORTX_UI_SETTINGROW_HPP */
