/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/M17View.hpp"
#include "views/TextInputView.hpp"
#include "base/Event.hpp"
#include "views/UiMemory.hpp"
#include "interfaces/delays.h"
#include "core/state.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"
#include "widgets/Charsets.hpp"

#include <cstdio>
#include <cstring>

namespace ortxui
{

void M17View::build()
{
    items[RowCallsign].label = STR(callsign);
    /* Meta text is longer than a value buffer: show the live settings text,
     * ellipsized by the list. */
    items[RowMeta].label = STR(metaText);
    items[RowMeta].value = state.settings.M17_meta_text;
    items[RowCan].label = STR(CAN);
    items[RowCanRx].label = STR(canRxCheck);
    items[RowCanRx].checkbox = true;
    items[RowCanRx].checked = state.settings.m17_can_rx;

    buildList(STR(m17), items.data(), RowCount);
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
    if (editing && (editRow == RowCallsign))
        callsign.formatBracketed(values(RowCallsign), VALUE_SIZE);
    else
        sniprintf(values(RowCallsign), VALUE_SIZE, "%s",
                  state.settings.callsign);
    listBox.invalidate();
}

void M17View::onBeginEdit(uint8_t row)
{
    if (row != RowCallsign)
        return;
    strncpy(callBuf, state.settings.callsign, sizeof(callBuf) - 1);
    callBuf[sizeof(callBuf) - 1] = '\0';
    bindCallsign();
    callsign.begin();
}

void M17View::bindCallsign()
{
    callsign.configure(callBuf, sizeof(callBuf), CHARSET_CALLSIGN,
                       TextInput::Mode::SingleLine, FONT_SIZE_8PT);
    callsign.setMultiTap(MTAP_CALLSIGN);
}

void M17View::saveCustom(EditBlob<CUSTOM_EDIT> &e) const
{
    CallsignEdit c;
    memcpy(c.text, callBuf, sizeof(c.text));
    c.cursor = callsign.cursor();
    e.put(c);
}

void M17View::restoreCustom(const EditBlob<CUSTOM_EDIT> &e)
{
    CallsignEdit c;
    e.get(c);
    memcpy(callBuf, c.text, sizeof(callBuf));
    bindCallsign();
    callsign.resume(c.cursor, 0);
    refreshCustom(RowCallsign, false);
}

void M17View::onAdjust(uint8_t row, int8_t dir)
{
    /* CAN is the only Stepper row. */
    (void)row;
    settings_t &st = state.settings;
    st.m17_can = static_cast<uint8_t>((st.m17_can + dir + 16) % 16);
    requestSyncRtx();
}

void M17View::onToggle(uint8_t row, bool on)
{
    /* CAN RX is the only Checkbox row. */
    (void)row;
    state.settings.m17_can_rx = on;
    requestSyncRtx();
}

void M17View::announceValue(uint8_t row, const UiText &value,
                            vpQueueFlags flags)
{
    if (row == RowCan)
        vp_announceSettingsInt(&currentLanguage->CAN, flags,
                               state.settings.m17_can);
    else
        SettingsListView::announceValue(row, value, flags);
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
        vp_announceInputChar(callsign.cursorChar());
}

void M17View::callsignConfirm()
{
    strncpy(state.settings.callsign, callBuf,
            sizeof(state.settings.callsign) - 1);
    state.settings.callsign[sizeof(state.settings.callsign) - 1] = '\0';
    requestSyncRtx();

    endEdit();
}

bool M17View::onEditEvent(const Event &e)
{
    /* Only the Callsign (Custom) row owns its editing stream; CAN falls through
     * to the base's generic stepper handling. */
    if (editRow != RowCallsign)
        return false;

    switch (callsign.handleEvent(e, getTick())) {
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
