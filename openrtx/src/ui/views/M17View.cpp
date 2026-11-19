/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/M17View.hpp"
#include "views/TextInputView.hpp"
#include "core/Event.hpp"
#include "core/UiMemory.hpp"
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
    items_[RowCallsign].label = STR(callsign);
    /* Meta text is longer than a value buffer: show the live settings text,
     * ellipsized by the list. */
    items_[RowMeta].label = STR(metaText);
    items_[RowMeta].value = state.settings.M17_meta_text;
    items_[RowCan].label = STR(CAN);
    items_[RowCanRx].label = STR(canRxCheck);
    items_[RowCanRx].checkbox = true;
    items_[RowCanRx].checked = state.settings.m17_can_rx;

    buildList(STR(m17), items_.data(), RowCount);
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
    /* CAN is the only Stepper row. */
    (void)row;
    sniprintf(out, cap, "%u", static_cast<unsigned>(state.settings.m17_can));
}

void M17View::refreshCustom(uint8_t row, bool speak)
{
    (void)row;
    (void)speak;
    if (editing_ && (editRow_ == RowCallsign))
        callsign_.formatBracketed(values(RowCallsign), VALUE_SIZE);
    else
        sniprintf(values(RowCallsign), VALUE_SIZE, "%s",
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
                        TextInput::Mode::SingleLine, FONT_SIZE_8PT);
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
                        TextInput::Mode::SingleLine, FONT_SIZE_8PT);
    callsign_.setMultiTap(MTAP_CALLSIGN);
    callsign_.resume(e.cursor, 0);
    refreshCustom(RowCallsign, false);
}

void M17View::onAdjust(uint8_t row, int8_t dir)
{
    /* CAN is the only Stepper row. */
    (void)row;
    settings_t &st = state.settings;
    st.m17_can = static_cast<uint8_t>((st.m17_can + dir + 16) % 16);
}

void M17View::onToggle(uint8_t row, bool on)
{
    /* CAN RX is the only Checkbox row. */
    (void)row;
    state.settings.m17_can_rx = on;
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

void M17View::callsignConfirm()
{
    strncpy(state.settings.callsign, callBuf_,
            sizeof(state.settings.callsign) - 1);
    state.settings.callsign[sizeof(state.settings.callsign) - 1] = '\0';

    endEdit();
}

bool M17View::onEditEvent(const Event &e)
{
    /* Only the Callsign (Custom) row owns its editing stream; CAN falls through
     * to the base's generic stepper handling. */
    if (editRow_ != RowCallsign)
        return false;

    switch (callsign_.handleEvent(e, getTick())) {
        case TextInput::KeyResult::Committed:
            callsignConfirm();
            break;

        case TextInput::KeyResult::Cancelled:
            endEdit();
            break;

        case TextInput::KeyResult::Editing:
            refreshCustom(RowCallsign, false);
            announceCursorChar();
            break;

        default:
            break;
    }
    return true;
}

} // namespace ortxui
