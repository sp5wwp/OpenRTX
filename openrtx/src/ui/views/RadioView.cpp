/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/RadioView.hpp"
#include "base/Event.hpp"
#include "views/UiMemory.hpp"
#include "interfaces/keyboard.h"
#include "core/utils.h"
#include "core/state.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ortxui
{

/** Largest offset the entry takes, in kHz. */
static constexpr uint32_t OFFSET_MAX_KHZ = 999999u;

/** Slot mask of the offset entry: six digits of kHz. */
static constexpr char OFFSET_MASK[] = "999999";

/** Unit of a frequency shown in its largest whole unit. */
struct FreqUnit {
    uint32_t div;    ///< Hz in one unit
    uint8_t width;   ///< Decimal digits of a fraction of the unit
    const char *sym; ///< Unit symbol
    uint16_t prompt; ///< Unit voice prompt
};

/** Units, largest first. */
static constexpr FreqUnit FREQ_UNITS[] = {
    { 1000000u, 6, "MHz", PROMPT_MEGAHERTZ },
    { 1000u, 3, "kHz", PROMPT_KILOHERTZ },
    { 1u, 0, "Hz", PROMPT_HERTZ },
};

/**
 * Format a frequency as a number in its largest whole unit, trailing zeros
 * stripped, e.g. 600000 -> "600" kHz, 6250 -> "6.25" kHz, 5000000 -> "5" MHz.
 *
 * @param hz: frequency in Hz.
 * @param buf: output buffer.
 * @param size: output buffer size.
 * @return the unit.
 */
static const FreqUnit &formatFreqNumber(uint32_t hz, char *buf, size_t size)
{
    size_t i = 0;
    while ((hz < FREQ_UNITS[i].div) && (FREQ_UNITS[i].div > 1u))
        i++;
    const FreqUnit &u = FREQ_UNITS[i];

    sniprintf(buf, size, "%u.%0*u", static_cast<unsigned>(hz / u.div), u.width,
              static_cast<unsigned>(hz % u.div));

    /* Strip trailing zeros, then a bare trailing dot. */
    size_t len = strlen(buf);
    while ((len > 0) && (buf[len - 1] == '0') && (strchr(buf, '.') != nullptr))
        buf[--len] = '\0';
    if ((len > 0) && (buf[len - 1] == '.'))
        buf[--len] = '\0';

    return u;
}

/**
 * Format a frequency with its unit, e.g. "6.25 kHz".
 *
 * @param hz: frequency in Hz.
 * @param buf: output buffer.
 * @param size: output buffer size.
 */
static void formatFreq(uint32_t hz, char *buf, size_t size)
{
    char num[16];
    const FreqUnit &u = formatFreqNumber(hz, num, sizeof(num));
    sniprintf(buf, size, "%s %s", num, u.sym);
}

/**
 * Speak a frequency setting: its name at the descriptive level, then the
 * value and its unit.
 *
 * @param name: setting name, an entry of currentLanguage.
 * @param hz: frequency in Hz.
 * @param flags: voice prompt queue flags.
 */
static void announceFreq(const char *const *name, uint32_t hz,
                         vpQueueFlags flags)
{
    if (state.settings.vpLevel < vpLow)
        return;

    char num[16];
    const FreqUnit &u = formatFreqNumber(hz, num, sizeof(num));
    vp_announceSettingsValue(name, num, u.prompt, flags);
}

/**
 * Lay a decimal value into a slot field left-aligned, so it reads back as that
 * integer via its filled prefix (filled slot count = number of digits).
 *
 * @param f: slot field.
 * @param val: value.
 */
static void setSlotsDecimal(TextInput &f, uint32_t val)
{
    char tmp[12];
    const int n = sniprintf(tmp, sizeof(tmp), "%u", static_cast<unsigned>(val));
    f.clearSlots();
    const uint16_t cap = f.slotCount();
    uint16_t i = 0;
    for (; (i < static_cast<uint16_t>(n)) && (i < cap); i++)
        f.setSlotDigit(i, tmp[i] - '0');
    f.setCursorSlot(i);
}

/**
 * Get the split of a channel.
 *
 * @param ch: channel.
 * @return the distance between its TX and RX frequencies, in Hz.
 */
static uint32_t splitOf(const channel_t &ch)
{
    return (ch.tx_frequency >= ch.rx_frequency) ?
               (ch.tx_frequency - ch.rx_frequency) :
               (ch.rx_frequency - ch.tx_frequency);
}

void RadioView::build()
{
    items[RowOffset].label = STR(offset);
    items[RowDirection].label = STR(direction);
    items[RowStep].label = STR(step);
    buildList(STR(radio), items.data(), RowCount);
}

RadioView::RowKind RadioView::rowKind(uint8_t row) const
{
    return (row == RowOffset) ? RowKind::Custom : RowKind::Stepper;
}

void RadioView::formatValue(uint8_t row, char *out, size_t cap)
{
    const channel_t &ch = state.channel;
    if (row == RowDirection)
        sniprintf(out, cap, "%c",
                  (ch.tx_frequency >= ch.rx_frequency) ? '+' : '-');
    else
        formatFreq(freq_steps[state.step_index], out, cap);
}

void RadioView::refreshCustom(uint8_t row, bool speak)
{
    (void)row;
    if (editing && (editRow == RowOffset)) {
        const uint32_t khz = offsetKhz();
        sniprintf(values(RowOffset), VALUE_SIZE, "<%u kHz>",
                  static_cast<unsigned>(khz));
        if (speak)
            announceFreq(&currentLanguage->offset, khz * 1000u,
                         vp_getVoiceLevelQueueFlags());
    } else {
        formatFreq(splitOf(state.channel), values(RowOffset), VALUE_SIZE);
    }
    listBox.invalidate();
}

void RadioView::bindOffset()
{
    offsetInput.configureSlots(offsetBuf, sizeof(offsetBuf), OFFSET_MASK, '-',
                               FONT_SIZE_8PT);
}

void RadioView::onBeginEdit(uint8_t row)
{
    (void)row;
    /* Pre-fill the keypad with the current split, so ENTER keeps it. */
    const channel_t &ch = state.channel;
    offsetNeg = (ch.tx_frequency < ch.rx_frequency);
    bindOffset();
    setSlotsDecimal(offsetInput, splitOf(ch) / 1000u);
    offsetPristine = true;
}

void RadioView::saveCustom(EditBlob<CUSTOM_EDIT> &e) const
{
    OffsetEdit o;
    memcpy(o.digits, offsetBuf, sizeof(o.digits));
    o.slot = static_cast<uint8_t>(offsetInput.cursorSlot());
    o.negative = offsetNeg;
    o.pristine = offsetPristine;
    e.put(o);
}

void RadioView::restoreCustom(const EditBlob<CUSTOM_EDIT> &e)
{
    OffsetEdit o;
    e.get(o);
    bindOffset();
    memcpy(offsetBuf, o.digits, sizeof(offsetBuf));
    offsetInput.setCursorSlot(o.slot);
    offsetNeg = o.negative;
    offsetPristine = o.pristine;
    refreshCustom(RowOffset, false);
}

void RadioView::onAdjust(uint8_t row, int8_t dir)
{
    channel_t &ch = state.channel;

    if (row == RowDirection) {
        /* Mirror the TX split about RX: tx' = 2*rx - tx. A simplex channel
         * (tx == rx) is unaffected. */
        const int64_t rx = ch.rx_frequency;
        const int64_t tx = ch.tx_frequency;
        const int64_t mirrored = 2 * rx - tx;
        if (mirrored > 0) {
            ch.tx_frequency = static_cast<freq_t>(mirrored);
            requestSyncRtx();
        }
    } else {
        /* The tuning step only changes the VFO increment: no rtx sync. */
        const int n = static_cast<int>(n_freq_steps);
        int s = (static_cast<int>(state.step_index) + dir) % n;
        if (s < 0)
            s += n;
        state.step_index = static_cast<uint8_t>(s);
    }
}

uint32_t RadioView::offsetKhz() const
{
    /* The filled slots read left-to-right as a decimal kHz value. */
    uint32_t v = 0;
    const uint16_t n = offsetInput.filledSlots();
    for (uint16_t o = 0; o < n; o++)
        v = v * 10u + static_cast<uint32_t>(offsetInput.slotDigit(o));
    return v;
}

void RadioView::offsetAdjust(int8_t dir)
{
    /* Nudge the offset by the tuning step, at least 1 kHz. */
    uint32_t stepKhz = freq_steps[state.step_index] / 1000u;
    if (stepKhz == 0u)
        stepKhz = 1u;

    uint32_t v = offsetKhz();
    if (dir > 0)
        v = (v + stepKhz > OFFSET_MAX_KHZ) ? OFFSET_MAX_KHZ : (v + stepKhz);
    else
        v = (v > stepKhz) ? (v - stepKhz) : 0u;

    setSlotsDecimal(offsetInput, v);
    offsetPristine = true; /* the nudged value becomes a fresh pre-fill */
    refreshCustom(RowOffset, true);
}

void RadioView::offsetApply()
{
    /* Apply the entered magnitude in the current direction (keeping a '-' split
     * negative); a TX frequency the radio cannot tune keeps the entry open. */
    const int64_t off = static_cast<int64_t>(offsetKhz()) * 1000;
    const int64_t tx = static_cast<int64_t>(state.channel.rx_frequency)
                     + (offsetNeg ? -off : off);
    if ((tx < 0) || (tx > UINT32_MAX) || !freqInBand(static_cast<freq_t>(tx))) {
        vp_announceError(vp_getVoiceLevelQueueFlags());
        return;
    }

    state.channel.tx_frequency = static_cast<freq_t>(tx);
    requestSyncRtx();
    endEdit();
    announceFreq(&currentLanguage->offset, static_cast<uint32_t>(off),
                 static_cast<vpQueueFlags>(vp_getVoiceLevelQueueFlags()
                                           | vpqIncludeDescriptions));
}

void RadioView::announceValue(uint8_t row, const UiText &value,
                              vpQueueFlags flags)
{
    if (row == RowStep)
        announceFreq(&currentLanguage->step, freq_steps[state.step_index],
                     flags);
    else
        SettingsListView::announceValue(row, value, flags);
}

void RadioView::announceKey(uint32_t keys)
{
    /* Speak the digit typed, or the one '*' removes. */
    if (state.settings.vpLevel < vpLow)
        return;

    const uint16_t n = offsetInput.filledSlots();
    if ((keys & KBD_NUM_MASK) != 0u)
        vp_announceInputChar(static_cast<char>('0' + __builtin_ctz(keys)));
    else if (n > 0)
        vp_announceInputChar(
            static_cast<char>('0' + offsetInput.slotDigit(n - 1)));
}

bool RadioView::onEditEvent(const Event &e)
{
    /* Direction and Step are plain Steppers: the base handles them. */
    if (editRow != RowOffset)
        return false;

    int8_t dir = 0;
    switch (e.kind) {
        case EvKind::Encoder:
            dir = e.encoder; /* knob nudges the offset by the tuning step */
            break;

        case EvKind::KeyRepeat:
            /* A held arrow keeps nudging; every other key acts once. */
            if ((e.keys & (KEY_UP | KEY_RIGHT)) != 0u)
                dir = +1;
            else if ((e.keys & (KEY_DOWN | KEY_LEFT)) != 0u)
                dir = -1;
            break;

        case EvKind::Key: {
            const uint32_t k = e.keys;
            if ((k & KEY_ENTER) != 0u) {
                offsetApply();
                return true;
            }
            if ((k & KEY_ESC) != 0u) {
                endEdit();
                return true;
            }
            if ((k & (KEY_UP | KEY_RIGHT)) != 0u) {
                dir = +1;
            } else if ((k & (KEY_DOWN | KEY_LEFT)) != 0u) {
                dir = -1;
            } else {
                /* Digits retype the value and '*' backspaces. */
                if ((k & (KBD_NUM_MASK | KEY_STAR)) != 0u) {
                    announceKey(k);
                    offsetInput.handlePrefilledKey(k, offsetPristine, 0);
                    refreshCustom(RowOffset, false);
                }
                return true;
            }
            break;
        }

        default:
            break;
    }
    if (dir != 0)
        offsetAdjust(dir);
    return true;
}

} // namespace ortxui
