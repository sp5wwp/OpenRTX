/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_UIMEMORY_HPP
#define ORTX_UI_UIMEMORY_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include "core/ViewId.hpp"
#include "core/UiText.hpp"
#include "core/messages.h"
#include "style/Charsets.hpp"
#include "widgets/List.hpp"

namespace ortxui
{

/**
 * A text field the modal editor is working on. The target outlives the views
 * on both sides of the edit: it is a settings field or a buffer owned here.
 */
struct EditRequest {
    UiText title;             ///< Editor title
    char *dst;                ///< Field a commit writes
    char *work;               ///< Working copy, cap bytes on the heap
    const Charset *charset;   ///< Characters the cursor cycles through
    const MultiTapTable *tap; ///< Keypad multi-tap table
    uint32_t picked;          ///< Code point from the picker, 0 if none
    uint16_t cap;             ///< Capacity of dst and work, with the NUL
    uint16_t cursor;          ///< Cursor position in work
    uint16_t topRow;          ///< First visible wrapped row
    uint8_t pickerSelected;   ///< Picker selection
    uint8_t pickerTop;        ///< Picker scroll row
    bool multiline;           ///< Field wraps over several lines
    bool rtx;                 ///< Commit reconfigures the radio
};

/**
 * Message being composed, on the heap while the compose form is open.
 */
struct ComposeDraft {
    char recipient[MSG_ADDR_MAX_LEN]; ///< Recipient, empty for broadcast
    char body[MSG_BODY_MAX_LEN];      ///< Body text
};

/**
 * Bytes holding a view's own edit state, which only that view reads back.
 */
template <size_t N>
struct EditBlob {
    /**
     * Store an edit state.
     *
     * @param v: edit state, plain data.
     */
    template <typename T>
    void put(const T &v)
    {
        static_assert(sizeof(T) <= N, "edit state too large");
        static_assert(std::is_trivially_copyable<T>::value,
                      "edit state must be plain data");
        memcpy(bytes, &v, sizeof(T));
    }

    /**
     * Read back an edit state stored by put().
     *
     * @param v: edit state of the type put() stored.
     */
    template <typename T>
    void get(T &v) const
    {
        static_assert(sizeof(T) <= N, "edit state too large");
        memcpy(&v, bytes, sizeof(T));
    }

    uint8_t bytes[N]; ///< The stored state
};

/**
 * In-place edit of a view the macro menu replaced, put back when it closes.
 */
struct InlineEdit {
    /**
     * Keep a view's edit state.
     *
     * @param id: view keeping it.
     * @param v: edit state, plain data.
     */
    template <typename T>
    void keep(ViewId id, const T &v)
    {
        state.put(v);
        owner = id;
    }

    /**
     * Take back the edit state a view kept, once.
     *
     * @param id: view taking it.
     * @param v: edit state of the type keep() stored.
     * @return false if the view kept none.
     */
    template <typename T>
    bool take(ViewId id, T &v)
    {
        if (owner != id)
            return false;
        owner = ViewId::Count;
        state.get(v);
        return true;
    }

    static constexpr size_t SIZE = 48; ///< Largest edit state a view keeps

    EditBlob<SIZE> state;              ///< Edit state of the owner
    ViewId owner;                      ///< Its keeper, ViewId::Count if none
};

/**
 * What survives a view being destroyed: list positions, pinned messages, the
 * editor's request and the drafts.
 */
struct UiMemory {
    /** Constructor. */
    UiMemory();

    /** Destructor. */
    ~UiMemory();

    /**
     * Start a compose draft, allocating it if needed.
     *
     * @param recipient: initial recipient, or nullptr for broadcast.
     * @param reply: whether the draft answers a received message.
     * @return false if the draft could not be allocated.
     */
    bool openDraft(const char *recipient, bool reply);

    /** Free the compose draft. */
    void closeDraft();

    /**
     * Ask the text editor to edit a field, allocating its working copy.
     *
     * @param title: editor title.
     * @param dst: field to edit.
     * @param cap: field capacity, including the NUL.
     * @param cs: characters the cursor cycles through.
     * @param tap: keypad multi-tap table.
     * @param multiline: whether the field wraps over several lines.
     * @param rtx: whether a commit reconfigures the radio.
     * @return false if the working copy could not be allocated.
     */
    bool openEdit(const UiText &title, char *dst, uint16_t cap,
                  const Charset &cs, const MultiTapTable &tap, bool multiline,
                  bool rtx);

    /** Free the editor's working copy. */
    void closeEdit();

    std::array<ListPos, VIEW_COUNT> lists; ///< List position of each view
    EditRequest edit;                      ///< Text editor request
    InlineEdit inlineEdit; ///< Edit of the view under the macro menu
    ComposeDraft *draft;   ///< Compose draft, nullptr when not composing
    uint32_t inboxPinned;  ///< Sequence pinned in the inbox
    uint32_t messageSeq;   ///< Sequence of the message shown in detail
    int16_t messageScroll; ///< Scroll offset of the message body
    bool draftReply;       ///< Draft answers a received message
    bool keypadLocked;     ///< Keys are ignored outside the macro menu
};

/** The UI memory. */
extern UiMemory uiMemory;

} // namespace ortxui

#endif /* ORTX_UI_UIMEMORY_HPP */
