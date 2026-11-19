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
 * One List row, composed from the layout engine instead of hand-painted. A List
 * reuses a single SettingRow as a flyweight: it binds the row to each item in
 * turn, sizes it, lays it out and paints it. Because the row is a Flex column of
 * a label and (for a value row) a value line, the engine supplies the vertical
 * padding and inter-line gutter, and heightFor() reports the content height plus
 * a uniform inset — so a plain row, a checkbox row and a two-line value row all
 * carry the SAME padding to their separators, differing only in height.
 *
 * The row paints its own selection fill, bottom separator and (for a boolean)
 * an anti-aliased checkbox; the label / value children paint after draw().
 *
 * A value row is adaptive: when the label and value fit side by side within the
 * row width they collapse onto ONE line (label left, value right, each centred),
 * just like a checkbox row; only when they do not fit does the value drop to a
 * second line. The decision is measured per row from the bound text and the
 * assigned width, so a short "Direction +" sits on one line while a long value
 * wraps — mixed row heights within a menu are expected.
 */
class SettingRow : public Flex
{
public:
    /** Destructor. */
    ~SettingRow() = default;

    SettingRow();

    enum class Kind : uint8_t {
        Plain,    //< a single label (submenu / channel / credit line)
        Value,    //< label + value; one line if they fit, else two
        Checkbox, //< a label with an anti-aliased tick box at the right
    };

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

    Size natural() const override;
    void onLayout() override;
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

    Label label_;
    Label value_;
    Kind kind_ = Kind::Plain;
    bool checked_ = false;
    bool selected_ = false;
    int16_t padY_ = 4;
    int16_t gap_ = 2;

    static constexpr int16_t kPadX = 6;   //< left inset for the label
    static constexpr int16_t kFitGap = 8; //< min gutter between label & value
                                          //  when they share one line
    /* Smaller fonts on the compact panels keep the list dense on a 64px
     * screen, mirroring the other size-class-aware widgets. */
    static constexpr fontSize_t kLabelFont =
        (sizeClass() == SizeClass::Regular) ? FONT_SIZE_8PT : FONT_SIZE_6PT;
    static constexpr fontSize_t kValueFont =
        (sizeClass() == SizeClass::Regular) ? FONT_SIZE_6PT : FONT_SIZE_5PT;
};

} // namespace ortxui

#endif /* ORTX_UI_SETTINGROW_HPP */
