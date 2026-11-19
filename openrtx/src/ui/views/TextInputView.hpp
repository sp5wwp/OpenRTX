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

/**
 * A generic full-screen modal text editor: a title, a scrolling TextInput
 * field, and a key hint. A caller opens it for a destination buffer, the user
 * edits a working copy with the cursor-cycle keys, and ENTER writes it back
 * (ESC discards). It is pushed like any other view (`NavIntent::push`); because
 * the render loop clears to Background and paints only the top view, it fully
 * covers whatever it was opened over.
 *
 * The request and the working copy live in UiMemory, so the editor can be
 * destroyed and rebuilt (character picker, macro menu) without losing the
 * edit. The destination outlives the opener's view: a settings field or a
 * buffer UiMemory owns. On commit the editor writes it and optionally requests
 * an rtx resync, so the opener's change-gated syncFromState picks it up on
 * return.
 */
class TextInputView : public View
{
public:
    void build() override;

    /**
     * Configure and prepare the editor. Call this immediately before returning
     * `NavIntent::push(ViewId::TextInput)` from the opener. `dst` holds up to
     * `cap`-1 chars; it is copied into a working buffer and only written back
     * on commit.
     *
     * @param title: editor title.
     * @param dst: destination buffer.
     * @param cap: destination capacity, including the NUL.
     * @param cs: character set for cursor cycling.
     * @param tap: multi-tap table for keypad entry.
     * @param multiline: whether the field wraps over several lines.
     * @param rtx: whether a commit reconfigures the radio.
     * @return false if the working copy could not be allocated (do not push).
     */
    static bool open(const UiText &title, char *dst, uint16_t cap,
                     const Charset &cs, const MultiTapTable &tap,
                     bool multiline, bool rtx);

    /** Keep the cursor and scroll of the edit while destroyed. */
    void save(UiMemory &m) const override;

    /** Insert the character picked while the editor was destroyed. */
    void onShow() override;
    void announce() override;
    NavIntent onEvent(const Event &e) override;

    Screen &screen() override
    {
        return screen_;
    }

private:
    /** Bind the field to the request in UiMemory, where it left off. */
    void configureField();

    void commit();
    void afterEdit();   //< re-render + announce the cursor character
    void refreshHint(); //< key hint line, reflecting the current edit mode

    Screen screen_;
    Flex root_;
    Label title_;
    TextInput field_;
    Label hint_;
};

} // namespace ortxui

#endif /* ORTX_UI_TEXTINPUTVIEW_HPP */
