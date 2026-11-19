/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/TimeDateView.hpp"
#include "core/Event.hpp"
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
    FIELD_DATE = 0,
    FIELD_MONTH = 2,
    FIELD_YEAR = 4,
    FIELD_HOUR = 6,
    FIELD_MINUTE = 8,
};

void TimeDateView::build()
{
    const bool regular = (sizeClass() == SizeClass::Regular);

    root_.setArea({ 0, 0, static_cast<uint16_t>(CONFIG_SCREEN_WIDTH),
                    static_cast<uint16_t>(CONFIG_SCREEN_HEIGHT) });
    root_.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(timeAndDate));

    /* Centred date-over-time stack with a small hint below. */
    body_.setAxis(Axis::Column);
    body_.setAlign(Align::Stretch);
    body_.setJustify(Justify::Center);
    body_.setGap(regular ? 6 : 2);
    body_.setGrow(1);

    const fontSize_t big = regular ? FONT_SIZE_12PT : FONT_SIZE_8PT;
    dateLabel_.setFont(big);
    dateLabel_.setAlign(TEXT_ALIGN_CENTER);
    dateLabel_.setColor(Sem::OnSurface);
    dateLabel_.setBasis(regular ? 26 : 16);

    timeLabel_.setFont(big);
    timeLabel_.setAlign(TEXT_ALIGN_CENTER);
    timeLabel_.setColor(Sem::OnSurface);
    timeLabel_.setBasis(regular ? 26 : 16);

    hint_.setFont(FONT_SIZE_6PT);
    hint_.setAlign(TEXT_ALIGN_CENTER);
    hint_.setColor(Sem::OnSurfaceMuted);
    hint_.setText(STR(hintSet));
    hint_.setBasis(12);

    body_.addChild(&dateLabel_);
    body_.addChild(&timeLabel_);
    body_.addChild(&hint_);

    root_.addChild(&bar);
    root_.addChild(&body_);
    screen_.addChild(&root_);
    root_.onLayout();

    showDisplay(state);
    screen_.markAllDirty();
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
    showText(dateLabel_, dateBuf_, text);
    sniprintf(text, sizeof(text), "%02d:%02d:%02d", t.hour, t.minute, t.second);
    showText(timeLabel_, timeBuf_, text);
}

void TimeDateView::syncFromState(const state_t &s)
{
    View::syncFromState(s); /* refresh the shared top bar */
    if (!editing_)
        showDisplay(s);
}

void TimeDateView::announce()
{
#ifdef CONFIG_RTC
    if (state.settings.vpLevel >= vpLow)
        vp_announceSettingsTimeDate();
#endif
}

void TimeDateView::beginEdit()
{
    editing_ = true;
    input_.configureSlots(inputBuf_, sizeof(inputBuf_), ENTRY_MASK, '_',
                          FONT_SIZE_8PT);
    hint_.setText(STR(hintDateTime));
    hint_.invalidate();
    refreshEdit();

    if (state.settings.vpLevel >= vpLow)
        vp_announceBuffer(&currentLanguage->timeAndDate, true, false,
                          STR(hintDateTime).str());
}

void TimeDateView::endEdit()
{
    editing_ = false;
    hint_.setText(STR(hintSet));
    hint_.invalidate();
    showDisplay(state);
}

void TimeDateView::refreshEdit()
{
    const TextInput &f = input_;
    char text[TEXT_SIZE];
    sniprintf(text, sizeof(text), "%c%c/%c%c/%c%c", f.slotChar(0),
              f.slotChar(1), f.slotChar(2), f.slotChar(3), f.slotChar(4),
              f.slotChar(5));
    showText(dateLabel_, dateBuf_, text);
    sniprintf(text, sizeof(text), "%c%c:%c%c:00", f.slotChar(6), f.slotChar(7),
              f.slotChar(8), f.slotChar(9));
    showText(timeLabel_, timeBuf_, text);
}

void TimeDateView::announceDigit() const
{
    if (state.settings.vpLevel < vpLow)
        return;

    /* Digits typed so far, the last one included. */
    const uint16_t n = input_.filledSlots();
    vp_flush();
    vp_queueInteger(input_.slotDigit(n - 1));
    if ((n == FIELD_MONTH) || (n == FIELD_YEAR))
        vp_queuePrompt(PROMPT_SLASH);
    if (n == FIELD_HOUR)
        vp_queueString("hh:mm",
                       static_cast<vpFlags>(vpAnnounceCommonSymbols
                                            | vpAnnounceLessCommonSymbols));
    if (n == FIELD_MINUTE)
        vp_queuePrompt(PROMPT_COLON);
    vp_play();
}

int8_t TimeDateView::field(uint8_t first) const
{
    return static_cast<int8_t>(input_.slotDigit(first) * 10
                               + input_.slotDigit(first + 1));
}

void TimeDateView::commit()
{
    datetime_t local = {};
    local.date = field(FIELD_DATE);
    local.month = field(FIELD_MONTH);
    local.year = static_cast<uint8_t>(field(FIELD_YEAR));
    local.hour = field(FIELD_HOUR);
    local.minute = field(FIELD_MINUTE);
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
    if (!editing_) {
        if ((k & KEY_ESC) != 0u)
            return NavIntent::pop();
        if ((k & KEY_ENTER) != 0u)
            beginEdit();
        return NavIntent::none();
    }

    if ((k & KEY_ESC) != 0u) {
        endEdit();
    } else if ((k & KEY_ENTER) != 0u) {
        if (input_.filledSlots() == input_.slotCount())
            commit();
    } else if ((k & (KBD_NUM_MASK | KEY_STAR)) != 0u) {
        /* A digit fills the next slot, '*' backspaces. */
        const uint16_t filled = input_.filledSlots();
        input_.handleKey(k, 0);
        refreshEdit();
        if (input_.filledSlots() > filled)
            announceDigit();
    }
    return NavIntent::none();
}

} // namespace ortxui
