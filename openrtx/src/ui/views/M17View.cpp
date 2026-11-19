/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/M17View.hpp"
#include "views/TextInputView.hpp"
#include "core/Event.hpp"
#include "core/UiMemory.hpp"
#include "core/Layout.hpp"
#include "interfaces/keyboard.h"
#include "interfaces/delays.h"
#include "core/state.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"
#include "style/Charsets.hpp"
#include "hwconfig.h"

#include <cstdio>
#include <cstring>

namespace ortxui
{

void M17View::build()
{
    static const UiText kLabels[RowCount] = {
        STR(callsign),
        STR(metaText),
        STR(CAN),
        STR(canRxCheck),
    };

    for (uint8_t i = 0; i < RowCount; i++) {
        items_[i].label = kLabels[i];
        items_[i].value = bufs_[i];
    }
    /* Meta Txt can be up to 52 chars — too long for the per-row scratch — so
     * point it straight at the live settings buffer and let the List ellipsize
     * it to fit the value column. */
    items_[RowMeta].value = state.settings.M17_meta_text;

    /* CAN RX (check the CAN on receive) is a boolean -> checkbox, not a value. */
    items_[RowCanRx].checkbox = true;
    items_[RowCanRx].checked = state.settings.m17_can_rx;
    items_[RowCanRx].value = nullptr;

    buildList(STR(m17), items_, RowCount);
    refreshCustom(RowCallsign);
    refreshValue(RowCan);
}

M17View::RowKind M17View::rowKind(uint8_t row) const
{
    switch (row) {
        case RowCallsign:
            return RowKind::Custom;
        case RowMeta:
            return RowKind::Action;
        case RowCanRx:
            return RowKind::Checkbox;
        default: /* RowCan */
            return RowKind::Stepper;
    }
}

void M17View::formatValue(uint8_t row, char *out, size_t cap)
{
    /* Only CAN is a Stepper value row. */
    if ((row == RowCan) && (cap > 0u))
        snprintf(out, cap, "%u", (unsigned)state.settings.m17_can);
    else if (cap > 0u)
        out[0] = '\0';
}

void M17View::setValueText(uint8_t row, const char *s)
{
    snprintf(bufs_[row], sizeof(bufs_[row]), "%s", s);
}

void M17View::refreshCustom(uint8_t row)
{
    if (row != RowCallsign)
        return;
    if (editing_ && (editRow_ == RowCallsign))
        callsign_.formatBracketed(bufs_[RowCallsign],
                                  sizeof(bufs_[RowCallsign]));
    else
        snprintf(bufs_[RowCallsign], sizeof(bufs_[RowCallsign]), "%s",
                 state.settings.callsign);
    list_.invalidate();
}

void M17View::onBeginEdit(uint8_t row)
{
    if (row != RowCallsign)
        return;
    strncpy(callBuf_, state.settings.callsign, sizeof(callBuf_) - 1);
    callBuf_[sizeof(callBuf_) - 1] = '\0';
    callsign_.configure(callBuf_, sizeof(callBuf_), CHARSET_CALLSIGN,
                        TextInput::Mode::SingleLine,
                        TextInput::CursorStyle::Bracket, FONT_SIZE_8PT);
    callsign_.setMultiTap(MTAP_CALLSIGN);
    callsign_.begin();
}

void M17View::saveCustom(ListEdit &e) const
{
    memcpy(e.text, callBuf_, sizeof(callBuf_));
    e.cursor = static_cast<uint8_t>(callsign_.cursor());
}

void M17View::restoreCustom(const ListEdit &e)
{
    memcpy(callBuf_, e.text, sizeof(callBuf_));
    callsign_.configure(callBuf_, sizeof(callBuf_), CHARSET_CALLSIGN,
                        TextInput::Mode::SingleLine,
                        TextInput::CursorStyle::Bracket, FONT_SIZE_8PT);
    callsign_.setMultiTap(MTAP_CALLSIGN);
    callsign_.resume(e.cursor, 0);
    refreshCustom(RowCallsign);
}

void M17View::onAdjust(uint8_t /*row*/, int dir)
{
    /* Only CAN is an adjustable value (CAN RX is a checkbox). */
    settings_t &st = state.settings;
    st.m17_can = (uint8_t)((st.m17_can + dir + 16) % 16);
    lastCan_ = st.m17_can;
}

void M17View::onToggle(uint8_t row, bool on)
{
    if (row == RowCanRx) {
        state.settings.m17_can_rx = on;
        lastCanRx_ = on;
    }
}

NavIntent M17View::onActivate(uint8_t row)
{
    /* Meta Txt is long free text: hand it to the shared modal editor. */
    if ((row == RowMeta)
        && TextInputView::open(STR(metaText), state.settings.M17_meta_text,
                               sizeof(state.settings.M17_meta_text),
                               CHARSET_TEXT, MTAP_TEXT, /*multiline=*/true,
                               /*rtx=*/false))
        return NavIntent::push(ViewId::TextInput);
    return NavIntent::none();
}

void M17View::announceCursorChar()
{
    /* Speak the character under the cursor (phonetically at higher verbosity)
     * so a callsign can be edited by ear. */
    if (state.settings.vpLevel >= vpLow)
        vp_announceInputChar(callsign_.cursorChar());
}

void M17View::callsignCycle(int dir)
{
    callsign_.cycle(dir);
    refreshCustom(RowCallsign);
    announceCursorChar();
}

void M17View::callsignMove(int dir)
{
    callsign_.moveCursor(dir);
    refreshCustom(RowCallsign);
    announceCursorChar();
}

void M17View::callsignConfirm()
{
    /* Strip trailing spaces, then commit. */
    callsign_.stripTrailingSpaces();

    strncpy(state.settings.callsign, callBuf_,
            sizeof(state.settings.callsign) - 1);
    state.settings.callsign[sizeof(state.settings.callsign) - 1] = '\0';
    memcpy(lastCall_, state.settings.callsign, sizeof(lastCall_));

    endEdit();
}

void M17View::syncFromState(const state_t &s)
{
    View::syncFromState(s); /* top bar */

    const settings_t &st = s.settings;
    bool changed = false;

    if (!(editing_ && editRow_ == RowCallsign)
        && (strncmp(st.callsign, lastCall_, sizeof(lastCall_)) != 0)) {
        memcpy(lastCall_, st.callsign, sizeof(lastCall_));
        refreshCustom(RowCallsign);
        changed = true;
    }
    /* Meta Txt is edited by the modal editor; its value points at the live
     * settings buffer, so just repaint when it changes on return. */
    if (strncmp(st.M17_meta_text, lastMeta_, sizeof(lastMeta_)) != 0) {
        memcpy(lastMeta_, st.M17_meta_text, sizeof(lastMeta_));
        changed = true;
    }
    if (!(editing_ && editRow_ == RowCan) && (st.m17_can != lastCan_)) {
        lastCan_ = st.m17_can;
        refreshValue(RowCan);
        changed = true;
    }
    if (st.m17_can_rx != lastCanRx_) {
        lastCanRx_ = st.m17_can_rx;
        items_[RowCanRx].checked = st.m17_can_rx;
        changed = true;
    }

    if (changed)
        list_.invalidate();
}

bool M17View::onEditEvent(const Event &e)
{
    /* Only the Callsign (Custom) row owns its editing stream; CAN falls through
     * to the base's generic stepper handling. */
    if (editRow_ != RowCallsign)
        return false;

    if (e.kind == EvKind::Encoder) {
        /* Knob moves the cursor on arrow-less radios, cycles elsewhere. */
        if (kbdHasArrows())
            callsignCycle(e.encoder);
        else
            callsignMove(e.encoder > 0 ? +1 : -1);
    } else if (e.kind == EvKind::Key) {
        const uint32_t k = e.keys;
        if ((k & KEY_ENTER) != 0u) {
            callsignConfirm();
        } else if ((k & KEY_ESC) != 0u) {
            endEdit();
        } else if (!kbdSpaceOnHash() && ((k & KEY_HASH) != 0u)) {
            /* # clears the field (unified across the text editors). */
            callsign_.clear();
            refreshCustom(RowCallsign);
            announceCursorChar();
        } else if ((k & KBD_CHAR_MASK) != 0u) {
            /* Numeric-keypad multi-tap; '*' backspaces. */
            const uint8_t ki =
                static_cast<uint8_t>(__builtin_ctz(k & KBD_CHAR_MASK));
            if (ki == 10u)
                callsign_.backspace();
            else
                callsign_.tapKey(ki, getTick());
            refreshCustom(RowCallsign);
            announceCursorChar();
        } else if ((k & KEY_UP) != 0u) {
            callsignCycle(+1);
        } else if ((k & KEY_DOWN) != 0u) {
            callsignCycle(-1);
        } else if ((k & KEY_LEFT) != 0u) {
            callsignMove(-1);
        } else if ((k & KEY_RIGHT) != 0u) {
            callsignMove(+1);
        }
    }
    return true;
}

} // namespace ortxui
