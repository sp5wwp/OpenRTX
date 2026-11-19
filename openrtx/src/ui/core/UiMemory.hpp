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
#include <cstdint>
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
 * Frequency or destination entry of the VFO screen.
 */
struct VfoEdit {
    long long errorAt;             ///< Tick of an out-of-band commit, 0 if none
    int32_t shift;                 ///< TX - RX at the start of the entry
    char text[2][10];              ///< RX and TX digits, or the destination
    std::array<uint8_t, 2> cursor; ///< Cursor of each field
    bool input;                    ///< Frequency entry open
    bool txField;                  ///< Editing TX rather than RX
    bool rxPristine;               ///< RX still pre-filled
    bool txPristine;               ///< TX still derived from RX
    bool destination;              ///< Destination entry open
};

/**
 * Row being edited in place in a settings list.
 */
struct ListEdit {
    char text[10];  ///< Text or digits of a custom row
    uint8_t cursor; ///< Cursor of a custom row
    uint8_t row;    ///< Row being edited
    bool negative;  ///< Custom row flag: negative offset
    bool pristine;  ///< Custom row flag: value still pre-filled
};

/**
 * In-place edit of a view the macro menu replaced, put back when it closes.
 */
struct InlineEdit {
    union {
        VfoEdit vfo;   ///< VFO screen entry
        ListEdit list; ///< Settings list entry
    };
    ViewId owner;      ///< View that saved the edit, ViewId::Count if none
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
};

/** The UI memory. */
extern UiMemory uiMemory;

} // namespace ortxui

#endif /* ORTX_UI_UIMEMORY_HPP */
