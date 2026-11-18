/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/TextInputView.hpp"
#include "views/PickerView.hpp"
#include "core/Event.hpp"
#include "core/Layout.hpp"
#include "interfaces/keyboard.h"
#include "interfaces/delays.h"
#include "core/state.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"
#include "hwconfig.h"

#include <cstring>

namespace ortxui
{

void TextInputView::build()
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;
    const bool regular = (sizeClass() == SizeClass::Regular);

    root_.setArea({ 0, 0, (uint16_t)W, (uint16_t)H });
    root_.setAxis(Axis::Column);
    root_.setPadding(regular ? 4 : 2);
    root_.setGap(regular ? 3 : 2);

    title_.setColor(Sem::Accent);
    title_.setFont(regular ? FONT_SIZE_8PT : FONT_SIZE_6PT);
    title_.setAlign(TEXT_ALIGN_CENTER);
    title_.setBasis(regular ? 14 : 10);

    field_.setGrow(1); /* the scrolling field fills the middle */

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

void TextInputView::open(const UiText &title, char *dst, uint16_t cap,
                         const Charset &cs, const MultiTapTable &tap,
                         bool multiline, bool rtx)
{
    dst_ = dst;
    rtx_ = rtx;

    /* Copy into the working buffer, bounded by both the caller's capacity and
     * our own storage. */
    uint16_t lim = (cap < sizeof(work_)) ? cap : (uint16_t)sizeof(work_);
    if (lim == 0)
        lim = 1;
    cap_ = lim;
    strncpy(work_, (dst != nullptr) ? dst : "", lim - 1);
    work_[lim - 1] = '\0';

    title_.setText(title);

    const bool regular = (sizeClass() == SizeClass::Regular);
    const fontSize_t f = regular ? FONT_SIZE_8PT : FONT_SIZE_6PT;
    field_.configure(work_, lim, cs,
                     multiline ? TextInput::Mode::MultiLine :
                                 TextInput::Mode::SingleLine,
                     TextInput::CursorStyle::Block, f);
    field_.setMultiTap(tap);
    field_.begin();
    field_.setEditMode(TextInput::EditMode::Overtype); /* the only model now */
    refreshHint();

    screen_.markAllDirty();
}

void TextInputView::onShow()
{
    /* Returning from the character picker: insert the chosen code point. */
    uint32_t cp = 0;
    if ((picker_ != nullptr) && picker_->takeResult(cp)) {
        field_.putCodePoint(cp); /* insert or overwrite, per the current mode */
        field_.invalidate();
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
    /* Where '#' is a command key (not the space key) it clears the field. */
    const bool canClear = kbdHasNumeric() && !kbdSpaceOnHash();

    if (kbdHasNumeric()) {
        if (canClear)
            hint_.setText(regular ? STR(hintKeypadClear) :
                                    STR(hintKeypadClearShort));
        else
            hint_.setText(regular ? STR(hintKeypadSpace) :
                                    STR(hintKeypadSpaceShort));
    } else {
        /* Key names only on the compact screen: nothing to translate. */
        hint_.setText(regular ? STR(hintArrows) : UiText("Up/Dn L/R"));
    }
}

void TextInputView::commit()
{
    field_.stripTrailingSpaces();
    if ((dst_ != nullptr) && (cap_ > 0)) {
        strncpy(dst_, work_, cap_ - 1);
        dst_[cap_ - 1] = '\0';
    }
    if (rtx_)
        requestSyncRtx();
}

NavIntent TextInputView::onEvent(const Event &e)
{
    /* The knob moves the cursor where there are no arrows (the only way to
     * reposition), and cycles the character where arrows already move it. */
    if (e.kind == EvKind::Encoder) {
        if (kbdHasArrows()) {
            field_.cycle(e.encoder);
        } else {
            field_.commitPending();
            field_.moveCursor(e.encoder > 0 ? +1 : -1);
        }
        afterEdit();
        return NavIntent::none();
    }

    /* A long-press opens the UTF-8 character picker (accented Latin, symbols). */
    if (e.kind == EvKind::KeyLong) {
        if (picker_ != nullptr) {
            picker_->open();
            return NavIntent::push(picker_);
        }
        return NavIntent::none();
    }

    if (e.kind != EvKind::Key)
        return NavIntent::none();

    const uint32_t k = e.keys;
    if ((k & KEY_ENTER) != 0u) {
        field_.commitPending();
        commit();
        return NavIntent::pop();
    }
    if ((k & KEY_ESC) != 0u)
        return NavIntent::pop();

    /* '#' clears the field, but only where it isn't the space key (otherwise it
     * stays space and falls through to multi-tap below). Checked before the
     * multi-tap block, which would otherwise consume '#'. */
    if (!kbdSpaceOnHash() && ((k & KEY_HASH) != 0u)) {
        field_.commitPending();
        field_.clear();
        afterEdit();
        return NavIntent::none();
    }

    /* Numeric-keypad multi-tap (additive — digit keys only exist on keypad
     * radios). '*' is backspace; the rest type via the ETSI table. */
    if ((k & KBD_CHAR_MASK) != 0u) {
        const uint8_t ki =
            static_cast<uint8_t>(__builtin_ctz(k & KBD_CHAR_MASK));
        if (ki == 10u) /* KEY_STAR */
            field_.backspace();
        else
            field_.tapKey(ki, getTick());
        afterEdit();
        return NavIntent::none();
    }

    /* Arrows (or their absence): move vs cursor-cycle the character. */
    if (kbdHasArrows()) {
        if ((k & KEY_LEFT) != 0u)
            field_.moveCursor(-1);
        else if ((k & KEY_RIGHT) != 0u)
            field_.moveCursor(+1);
        else if ((k & KEY_UP) != 0u)
            field_.cycle(+1);
        else if ((k & KEY_DOWN) != 0u)
            field_.cycle(-1);
        else
            return NavIntent::none();
    } else {
        if ((k & KEY_UP) != 0u) {
            field_.commitPending();
            field_.moveCursor(-1);
        } else if ((k & KEY_DOWN) != 0u) {
            field_.commitPending();
            field_.moveCursor(+1);
        } else {
            return NavIntent::none();
        }
    }
    afterEdit();
    return NavIntent::none();
}

} // namespace ortxui
