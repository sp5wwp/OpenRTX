/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/TimeDateView.hpp"
#include "base/Event.hpp"
#include "views/UiMemory.hpp"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"
#include "interfaces/keyboard.h"
#include "interfaces/platform.h"
#include "hwconfig.h"

#include <cstdio>
#include <cstring>

namespace ortxui
{

/** Slot mask of the entry: dd mm yy hh mm. */
static constexpr char ENTRY_MASK[] = "9999999999";

/** Entry slot of each field's first digit. */
enum EntryField : uint8_t {
    FieldDate = 0,   ///< Day of the month
    FieldMonth = 2,  ///< Month
    FieldYear = 4,   ///< Year in the century
    FieldHour = 6,   ///< Hour
    FieldMinute = 8, ///< Minute
};

void TimeDateView::build()
{
    const bool regular = (sizeClass() == SizeClass::Regular);

    root.setArea(screenArea());
    root.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(timeAndDate));

    /* Centred date-over-time stack with a small hint below. */
    body.setAxis(Axis::Column);
    body.setAlign(Align::Stretch);
    body.setJustify(Justify::Center);
    body.setGap(regular ? 6 : 2);
    body.setGrow(1);

    const fontSize_t big = regular ? FONT_SIZE_12PT : FONT_SIZE_8PT;
    dateLabel.setFont(big);
    dateLabel.setAlign(TEXT_ALIGN_CENTER);
    dateLabel.setColor(Sem::OnSurface);
    dateLabel.setBasis(regular ? 26 : 16);

    timeLabel.setFont(big);
    timeLabel.setAlign(TEXT_ALIGN_CENTER);
    timeLabel.setColor(Sem::OnSurface);
    timeLabel.setBasis(regular ? 26 : 16);

    hint.setFont(FONT_SIZE_6PT);
    hint.setAlign(TEXT_ALIGN_CENTER);
    hint.setColor(Sem::OnSurfaceMuted);
    hint.setText(STR(hintSet));
    hint.setBasis(12);

    body.addChild(&dateLabel);
    body.addChild(&timeLabel);
    body.addChild(&hint);

    root.addChild(&bar);
    root.addChild(&body);
    tree.addChild(&root);
    root.onLayout();

    showDisplay(state);
    tree.markAllDirty();
}

void TimeDateView::showText(Label &label, char *buf, const char *text)
{
    if (strncmp(buf, text, TEXT_SIZE) == 0)
        return;
    sniprintf(buf, TEXT_SIZE, "%s", text);
    label.setText(buf);
    label.invalidate();
}

void TimeDateView::showDisplay(const state_t &s)
{
    const datetime_t t = utcToLocalTime(s.time, s.settings.utc_timezone);

    char text[TEXT_SIZE];
    sniprintf(text, sizeof(text), "%02d/%02d/%02d", t.date, t.month, t.year);
    showText(dateLabel, dateBuf, text);
    sniprintf(text, sizeof(text), "%02d:%02d:%02d", t.hour, t.minute, t.second);
    showText(timeLabel, timeBuf, text);
}

void TimeDateView::syncFromState(const state_t &s)
{
    View::syncFromState(s); /* refresh the shared top bar */
    if (!editing)
        showDisplay(s);
}

void TimeDateView::announce()
{
#ifdef CONFIG_RTC
    if (state.settings.vpLevel >= vpLow)
        vp_announceSettingsTimeDate();
#endif
}

void TimeDateView::save() const
{
    if (!editing)
        return;

    DateEdit e;
    memcpy(e.digits, inputBuf, sizeof(e.digits));
    e.slot = input.cursorSlot();
    uiMemory.inlineEdit.keep(id(), e);
}

void TimeDateView::restore()
{
    DateEdit e;
    if (!uiMemory.inlineEdit.take(id(), e))
        return;

    openEntry();
    memcpy(inputBuf, e.digits, sizeof(inputBuf));
    input.setCursorSlot(e.slot);
    refreshEdit();
}

void TimeDateView::openEntry()
{
    editing = true;
    input.configureSlots(inputBuf, sizeof(inputBuf), ENTRY_MASK, '_',
                         FONT_SIZE_8PT);
    hint.setText(STR(hintDateTime));
    hint.invalidate();
    refreshEdit();
}

void TimeDateView::beginEdit()
{
    openEntry();
    if (state.settings.vpLevel >= vpLow)
        vp_announceBuffer(&currentLanguage->timeAndDate, true, false,
                          STR(hintDateTime).str());
}

void TimeDateView::endEdit()
{
    editing = false;
    hint.setText(STR(hintSet));
    hint.invalidate();
    showDisplay(state);
}

void TimeDateView::refreshEdit()
{
    const TextInput &f = input;
    char text[TEXT_SIZE];
    sniprintf(text, sizeof(text), "%c%c/%c%c/%c%c", f.slotChar(0),
              f.slotChar(1), f.slotChar(2), f.slotChar(3), f.slotChar(4),
              f.slotChar(5));
    showText(dateLabel, dateBuf, text);
    sniprintf(text, sizeof(text), "%c%c:%c%c:00", f.slotChar(6), f.slotChar(7),
              f.slotChar(8), f.slotChar(9));
    showText(timeLabel, timeBuf, text);
}

void TimeDateView::announceDigit() const
{
    if (state.settings.vpLevel < vpLow)
        return;

    /* Digits typed so far, the last one included. */
    const uint16_t n = input.filledSlots();
    vp_flush();
    vp_queueInteger(input.slotDigit(n - 1));
    if ((n == FieldMonth) || (n == FieldYear))
        vp_queuePrompt(PROMPT_SLASH);
    if (n == FieldHour)
        vp_queueString("hh:mm",
                       static_cast<vpFlags>(vpAnnounceCommonSymbols
                                            | vpAnnounceLessCommonSymbols));
    if (n == FieldMinute)
        vp_queuePrompt(PROMPT_COLON);
    vp_play();
}

int8_t TimeDateView::field(uint8_t first) const
{
    return static_cast<int8_t>(input.slotDigit(first) * 10
                               + input.slotDigit(first + 1));
}

void TimeDateView::commit()
{
    datetime_t local = {};
    local.date = field(FieldDate);
    local.month = field(FieldMonth);
    local.year = static_cast<uint8_t>(field(FieldYear));
    local.hour = field(FieldHour);
    local.minute = field(FieldMinute);
    if (!isValidTimeInfo(&local)) {
        vp_announceError(vp_getVoiceLevelQueueFlags());
        return;
    }

    /* The entry is local time; the clock keeps UTC. */
    const datetime_t utc = localTimeToUtc(local, state.settings.utc_timezone);
#ifdef CONFIG_RTC
    platform_setTime(utc);
#endif
    state.time = utc;
    endEdit();

#ifdef CONFIG_RTC
    if (state.settings.vpLevel >= vpLow)
        vp_announceSettingsTimeDate();
#endif
}

NavIntent TimeDateView::onEvent(const Event &e)
{
    if (e.kind != EvKind::Key)
        return NavIntent::none();

    const uint32_t k = e.keys;
    if (!editing) {
        if ((k & KEY_ESC) != 0u)
            return NavIntent::pop();
        if ((k & KEY_ENTER) != 0u)
            beginEdit();
        return NavIntent::none();
    }

    if ((k & KEY_ESC) != 0u) {
        endEdit();
    } else if ((k & KEY_ENTER) != 0u) {
        if (input.filledSlots() == input.slotCount())
            commit();
    } else if ((k & (KBD_NUM_MASK | KEY_STAR)) != 0u) {
        /* A digit fills the next slot, '*' backspaces. */
        const uint16_t filled = input.filledSlots();
        input.handleKey(k, 0);
        refreshEdit();
        if (input.filledSlots() > filled)
            announceDigit();
    }
    return NavIntent::none();
}

} // namespace ortxui
