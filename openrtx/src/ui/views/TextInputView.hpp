/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_TEXTINPUTVIEW_HPP
#define ORTX_UI_TEXTINPUTVIEW_HPP

#include <cstdint>
#include "views/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Label.hpp"
#include "widgets/TextInput.hpp"
#include "widgets/Charsets.hpp"

namespace ortxui
{

/**
 * Full-screen text editor: a title, a scrolling field and a key hint. It edits
 * a working copy of a destination buffer; ENTER writes it back and ESC drops
 * it. The request and the working copy live in UiMemory, so the editor can be
 * destroyed and rebuilt under the character picker or the macro menu.
 */
class TextInputView : public View
{
public:
    /** Constructor. */
    TextInputView() = default;

    /** Destructor. */
    ~TextInputView() = default;

    /** Lay out the editor and bind the field to the open request. */
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
    void save() const override;

    /** Put in the character picked while the editor was destroyed. */
    void onShow() override;

    /** Speak the title. */
    void announce() override;

    /**
     * Edit the field, open the picker on a long digit, or close the editor.
     *
     * @param e: event.
     * @return the navigation intent.
     */
    NavIntent onEvent(const Event &e) override;

    /** Screen the editor paints. */
    Screen &screen() override
    {
        return screen_;
    }

private:
    /** Bind the field to the request in UiMemory, where it left off. */
    void configureField();

    /** Write the working copy to the destination. */
    void commit();

    /** Repaint the field and speak the character under the cursor. */
    void afterEdit();

    /** Show the key hint for the radio's keypad. */
    void refreshHint();

    Screen screen_;   ///< Screen of the editor
    Flex root_;       ///< Column of title, field and hint
    Label title_;     ///< What is being edited
    TextInput field_; ///< The text
    Label hint_;      ///< Keys that edit it
};

} // namespace ortxui

#endif /* ORTX_UI_TEXTINPUTVIEW_HPP */
