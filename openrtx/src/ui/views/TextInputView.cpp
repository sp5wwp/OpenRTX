/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/TextInputView.hpp"
#include "views/PickerView.hpp"
#include "core/Event.hpp"
#include "core/UiMemory.hpp"
#include "core/Keypad.hpp"
#include "core/Layout.hpp"
#include "interfaces/keyboard.h"
#include "interfaces/delays.h"
#include "core/state.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"

#include <cstring>

namespace ortxui
{

void TextInputView::build()
{
    const bool regular = (sizeClass() == SizeClass::Regular);

    root_.setArea(screenArea());
    root_.setAxis(Axis::Column);
    root_.setPadding(regular ? 4 : 2);
    root_.setGap(regular ? 3 : 2);

    title_.setColor(Sem::Accent);
    title_.setFont(regular ? FONT_SIZE_8PT : FONT_SIZE_6PT);
    title_.setAlign(TEXT_ALIGN_CENTER);
    title_.setBasis(regular ? 14 : 10);

    field_.setGrow(1);
    configureField();

    hint_.setColor(Sem::OnSurfaceMuted);
    hint_.setFont(regular ? FONT_SIZE_6PT : FONT_SIZE_5PT);
    hint_.setAlign(TEXT_ALIGN_CENTER);
    hint_.setBasis(regular ? 10 : 8);
    refreshHint();

    root_.addChild(&title_);
    root_.addChild(&field_);
    root_.addChild(&hint_);
    screen_.addChild(&root_);
    root_.onLayout();

    screen_.markAllDirty();
}

bool TextInputView::open(const UiText &title, char *dst, uint16_t cap,
                         const Charset &cs, const MultiTapTable &tap,
                         bool multiline, bool rtx)
{
    return uiMemory.openEdit(title, dst, cap, cs, tap, multiline, rtx);
}

void TextInputView::configureField()
{
    const EditRequest &r = uiMemory.edit;
    if (r.work == nullptr)
        return;

    title_.setText(r.title);

    const bool regular = (sizeClass() == SizeClass::Regular);
    const fontSize_t f = regular ? FONT_SIZE_8PT : FONT_SIZE_6PT;
    field_.configure(r.work, r.cap, *r.charset,
                     r.multiline ? TextInput::Mode::MultiLine :
                                   TextInput::Mode::SingleLine,
                     f);
    field_.setMultiTap(*r.tap);
    field_.resume(r.cursor, r.topRow);
}

void TextInputView::save() const
{
    uiMemory.edit.cursor = field_.cursor();
    uiMemory.edit.topRow = field_.topRow();
}

void TextInputView::onShow()
{
    /* The picked character replaces the one under the cursor. */
    EditRequest &r = uiMemory.edit;
    if (r.picked != 0) {
        field_.putCodePoint(r.picked);
        field_.invalidate();
        r.picked = 0;
    }
}

void TextInputView::announce()
{
    vpSay(title_.text());
}

void TextInputView::afterEdit()
{
    field_.invalidate();
    if (state.settings.vpLevel >= vpLow)
        vp_announceInputChar(field_.cursorChar());
}

void TextInputView::refreshHint()
{
    const bool regular = (sizeClass() == SizeClass::Regular);

    if (kbdHasNumeric() && kbdSpaceOnHash())
        hint_.setText(regular ? STR(hintKeypadSpace) :
                                STR(hintKeypadSpaceShort));
    else if (kbdHasNumeric())
        hint_.setText(regular ? STR(hintKeypadClear) :
                                STR(hintKeypadClearShort));
    else if (kbdHasArrows())
        hint_.setText(regular ? STR(hintArrows) : STR(hintArrowsShort));
    else
        hint_.setText(STR(hintKnob));
}

void TextInputView::commit()
{
    const EditRequest &r = uiMemory.edit;
    strncpy(r.dst, r.work, r.cap - 1);
    r.dst[r.cap - 1] = '\0';
    if (r.rtx)
        requestSyncRtx();
}

NavIntent TextInputView::onEvent(const Event &e)
{
    /* A long press of a digit opens the character picker, whose choice then
     * replaces the letter the press typed. */
    if ((e.kind == EvKind::KeyLong) && ((e.keys & KBD_NUM_MASK) != 0u)) {
        PickerView::open();
        return NavIntent::push(ViewId::Picker);
    }

    switch (field_.handleEvent(e, getTick())) {
        case TextInput::KeyResult::Committed:
            commit();
            uiMemory.closeEdit();
            return NavIntent::pop();

        case TextInput::KeyResult::Cancelled:
            uiMemory.closeEdit();
            return NavIntent::pop();

        case TextInput::KeyResult::Editing:
            afterEdit();
            break;

        default:
            break;
    }
    return NavIntent::none();
}

} // namespace ortxui
