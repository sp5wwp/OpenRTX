/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_PICKERVIEW_HPP
#define ORTX_UI_PICKERVIEW_HPP

#include <cstdint>
#include "core/View.hpp"
#include "core/Object.hpp"

namespace ortxui
{

/**
 * The grid of pickable code points (a leaf object so the Screen paints it).
 * Holds the selection + vertical scroll and knows how to navigate and paint a
 * grid of glyphs from the custom icon/fallback font.
 */
class PickerGrid : public Object
{
public:
    /**
     * Put back a selection and scroll row.
     *
     * @param sel: selected cell.
     * @param top: first visible row.
     */
    void setPosition(uint16_t sel, uint16_t top)
    {
        sel_ = sel;
        top_ = top;
    }

    /** Selected cell. */
    uint16_t selected() const
    {
        return sel_;
    }

    /** First visible row. */
    uint16_t top() const
    {
        return top_;
    }

    void move(int delta); //< step the selection, scrolling as needed
    uint32_t selectedCodePoint() const;

    void draw(DrawCtx &d) override;

private:
    uint16_t cols() const;
    void ensureVisible();

    uint16_t sel_ = 0;
    uint16_t top_ = 0; //< first visible row
};

/**
 * A full-screen character picker: a scrolling grid of code points (accented
 * Latin + symbols not on the keyboard) drawn from the custom icon/fallback
 * font. Pushed on top of the text editor; ENTER selects and pops, ESC cancels.
 * The selection and the choice go to the editor's request in UiMemory, which
 * the editor reads when it is rebuilt.
 * Navigated by arrows/knob (LEFT/RIGHT step, UP/DOWN by row).
 */
class PickerView : public View
{
public:
    void build() override;

    /** Start a pick at the first cell. Call before pushing the picker. */
    static void open();

    /** Keep the selection while destroyed. */
    void save(UiMemory &m) const override;

    void announce() override;
    NavIntent onEvent(const Event &e) override;

    Screen &screen() override
    {
        return screen_;
    }

private:
    Screen screen_;
    PickerGrid grid_;
};

} // namespace ortxui

#endif /* ORTX_UI_PICKERVIEW_HPP */
