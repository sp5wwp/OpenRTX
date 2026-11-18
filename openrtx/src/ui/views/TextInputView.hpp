/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_TEXTINPUTVIEW_HPP
#define ORTX_UI_TEXTINPUTVIEW_HPP

#include <cstdint>
#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Widgets.hpp"
#include "widgets/TextInput.hpp"
#include "style/Charsets.hpp"

namespace ortxui
{

class PickerView;

/**
 * A generic full-screen modal text editor: a title, a scrolling TextInput
 * field, and a key hint. A caller opens it for a destination buffer, the user
 * edits a working copy with the cursor-cycle keys, and ENTER writes it back
 * (ESC discards). It is pushed like any other view (`NavIntent::push`); because
 * the render loop clears to Background and paints only the top view, it fully
 * covers whatever it was opened over.
 *
 * Result delivery matches the existing in-place editors: on commit it writes
 * the destination buffer (a settings field) and optionally requests an rtx
 * resync, so the opener's change-gated syncFromState picks it up on return.
 */
class TextInputView : public View
{
public:
    void build();

    /**
     * Configure and prepare the editor. Call this immediately before returning
     * `NavIntent::push(&editor)` from the opener. `dst` holds up to `cap`-1
     * chars; it is copied into a working buffer and only written back on commit.
     *
     * @param title: editor title.
     * @param dst: destination buffer.
     * @param cap: destination capacity, including the NUL.
     * @param cs: character set for cursor cycling.
     * @param tap: multi-tap table for keypad entry.
     * @param multiline: whether the field wraps over several lines.
     * @param rtx: whether a commit reconfigures the radio.
     */
    void open(const UiText &title, char *dst, uint16_t cap, const Charset &cs,
              const MultiTapTable &tap, bool multiline, bool rtx);

    /** Wire the shared UTF-8 character picker (opened by a long-press). */
    void setPicker(PickerView *p)
    {
        picker_ = p;
    }

    void onShow() override;
    void announce() override;
    NavIntent onEvent(const Event &e) override;

    Screen &screen() override
    {
        return screen_;
    }

private:
    void commit();
    void afterEdit();   //< re-render + announce the cursor character
    void refreshHint(); //< key hint line, reflecting the current edit mode

    Screen screen_;
    Flex root_;
    Label title_;
    TextInput field_;
    Label hint_;

    char work_[822] = {}; //< working copy (sized for the M17 SMS budget)
    char *dst_ = nullptr;
    uint16_t cap_ = 0;
    bool rtx_ = false;
    PickerView *picker_ = nullptr; //< shared UTF-8 character picker
};

} // namespace ortxui

#endif /* ORTX_UI_TEXTINPUTVIEW_HPP */
