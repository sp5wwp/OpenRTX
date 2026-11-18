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
    void reset()
    {
        sel_ = 0;
        top_ = 0;
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
 * The opener retrieves the choice with takeResult() when it regains focus.
 * Navigated by arrows/knob (LEFT/RIGHT step, UP/DOWN by row).
 */
class PickerView : public View
{
public:
    void build();
    void open(); //< reset selection/scroll before pushing

    void announce() override;
    NavIntent onEvent(const Event &e) override;

    Screen &screen() override
    {
        return screen_;
    }

    /** If a code point was selected since the last call, store it in `cp` and
     *  return true (clearing the pending result); otherwise return false. */
    bool takeResult(uint32_t &cp);

private:
    Screen screen_;
    PickerGrid grid_;
    uint32_t result_ = 0;
    bool resultReady_ = false;
};

} // namespace ortxui

#endif /* ORTX_UI_PICKERVIEW_HPP */
