/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/TextInputView.hpp"
#include "views/PickerView.hpp"
#include "base/Event.hpp"
#include "views/UiMemory.hpp"
#include "base/Keypad.hpp"
#include "base/Layout.hpp"
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
    const bool regular = (sizeClass() >= SizeClass::Regular);

    root.setArea(screenArea());
    root.setAxis(Axis::Column);
    root.setPadding(regular ? 4 : 2);
    root.setGap(regular ? 3 : 2);

    titleLabel.setColor(Sem::Accent);
    titleLabel.setFont(regular ? FONT_SIZE_8PT : FONT_SIZE_6PT);
    titleLabel.setAlign(TEXT_ALIGN_CENTER);
    titleLabel.setBasis(regular ? 14 : 10);

    field.setGrow(1);
    configureField();

    hint.setColor(Sem::OnSurfaceMuted);
    hint.setFont(regular ? FONT_SIZE_6PT : FONT_SIZE_5PT);
    hint.setAlign(TEXT_ALIGN_CENTER);
    hint.setBasis(regular ? 10 : 8);
    refreshHint();

    root.addChild(&titleLabel);
    root.addChild(&field);
    root.addChild(&hint);
    tree.addChild(&root);
    root.onLayout();

    tree.markAllDirty();
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

    titleLabel.setText(r.title);

    const bool regular = (sizeClass() >= SizeClass::Regular);
    const fontSize_t f = regular ? FONT_SIZE_8PT : FONT_SIZE_6PT;
    field.configure(r.work, r.cap, *r.charset,
                    r.multiline ? TextInput::Mode::MultiLine :
                                  TextInput::Mode::SingleLine,
                    f);
    field.setMultiTap(*r.tap);
    field.resume(r.cursor, r.topRow);
}

void TextInputView::save() const
{
    uiMemory.edit.cursor = field.cursor();
    uiMemory.edit.topRow = field.topRow();
}

void TextInputView::onShow()
{
    /* The picked character replaces the one under the cursor. */
    EditRequest &r = uiMemory.edit;
    if (r.picked != 0) {
        field.putCodePoint(r.picked);
        field.invalidate();
        r.picked = 0;
    }
}

void TextInputView::announce()
{
    vpSay(titleLabel.text());
}

void TextInputView::afterEdit()
{
    field.invalidate();
    if (state.settings.vpLevel >= vpLow)
        vp_announceInputChar(field.cursorChar());
}

void TextInputView::refreshHint()
{
    const bool regular = (sizeClass() >= SizeClass::Regular);

    if (kbdHasNumeric() && kbdSpaceOnHash())
        hint.setText(regular ? STR(hintKeypadSpace) :
                               STR(hintKeypadSpaceShort));
    else if (kbdHasNumeric())
        hint.setText(regular ? STR(hintKeypadClear) :
                               STR(hintKeypadClearShort));
    else if (kbdHasArrows())
        hint.setText(regular ? STR(hintArrows) : STR(hintArrowsShort));
    else
        hint.setText(STR(hintKnob));
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

    switch (field.handleEvent(e, getTick())) {
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
