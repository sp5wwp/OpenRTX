/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/RadioView.hpp"
#include "core/Event.hpp"
#include "core/UiMemory.hpp"
#include "interfaces/keyboard.h"
#include "core/utils.h"
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

/**
 * Format a frequency in Hz as a compact "N.NN kHz/MHz" (trailing zeros
 * stripped), e.g. 600000 -> "600 kHz", 6250 -> "6.25 kHz", 5000000 -> "5 MHz".
 * Sub-kHz values fall back to "N Hz".
 *
 * @param hz: frequency in Hz.
 * @param buf: output buffer.
 * @param size: output buffer size.
 */
static void formatFreq(uint32_t hz, char *buf, size_t size)
{
    uint32_t div;
    int width;
    char prefix;

    if (hz >= 1000000u) {
        div = 1000000u;
        width = 6;
        prefix = 'M';
    } else if (hz >= 1000u) {
        div = 1000u;
        width = 3;
        prefix = 'k';
    } else {
        sniprintf(buf, size, "%u Hz", static_cast<unsigned>(hz));
        return;
    }

    char tmp[16];
    sniprintf(tmp, sizeof(tmp), "%u.%0*u", static_cast<unsigned>(hz / div),
              width, static_cast<unsigned>(hz % div));

    /* Strip trailing zeros, then a bare trailing dot. */
    size_t len = strlen(tmp);
    while ((len > 0) && (tmp[len - 1] == '0'))
        tmp[--len] = '\0';
    if ((len > 0) && (tmp[len - 1] == '.'))
        tmp[--len] = '\0';

    sniprintf(buf, size, "%s %cHz", tmp, prefix);
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
    items_[RowOffset].label = STR(offset);
    items_[RowDirection].label = STR(direction);
    items_[RowStep].label = STR(step);
    buildList(STR(radio), items_.data(), RowCount);
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
    if (editing_ && (editRow_ == RowOffset)) {
        const unsigned v = static_cast<unsigned>(offsetKhz());
        sniprintf(values(RowOffset), VALUE_SIZE, "<%u kHz>", v);
        if (speak) {
            char clean[16];
            sniprintf(clean, sizeof(clean), "%u kHz", v);
            vpSay(clean);
        }
    } else {
        formatFreq(splitOf(state.channel), values(RowOffset), VALUE_SIZE);
    }
    list_.invalidate();
}

void RadioView::bindOffset()
{
    offsetInput_.configureSlots(offsetBuf_, sizeof(offsetBuf_), OFFSET_MASK,
                                '-', FONT_SIZE_8PT);
}

void RadioView::onBeginEdit(uint8_t row)
{
    (void)row;
    /* Pre-fill the keypad with the current split, so ENTER keeps it. */
    const channel_t &ch = state.channel;
    offsetNeg_ = (ch.tx_frequency < ch.rx_frequency);
    bindOffset();
    setSlotsDecimal(offsetInput_, splitOf(ch) / 1000u);
    offsetPristine_ = true;
}

void RadioView::saveCustom(ListEdit &e) const
{
    memcpy(e.text, offsetBuf_, sizeof(offsetBuf_));
    e.cursor = static_cast<uint8_t>(offsetInput_.cursorSlot());
    e.negative = offsetNeg_;
    e.pristine = offsetPristine_;
}

void RadioView::restoreCustom(const ListEdit &e)
{
    bindOffset();
    memcpy(offsetBuf_, e.text, sizeof(offsetBuf_));
    offsetInput_.setCursorSlot(e.cursor);
    offsetNeg_ = e.negative;
    offsetPristine_ = e.pristine;
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
    const uint16_t n = offsetInput_.filledSlots();
    for (uint16_t o = 0; o < n; o++)
        v = v * 10u + static_cast<uint32_t>(offsetInput_.slotDigit(o));
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

    setSlotsDecimal(offsetInput_, v);
    offsetPristine_ = true; /* the nudged value becomes a fresh pre-fill */
    refreshCustom(RowOffset, true);
}

void RadioView::offsetApply()
{
    /* Apply the entered magnitude in the current direction (keeping a '-' split
     * negative); a TX frequency the radio cannot tune keeps the entry open. */
    const int64_t off = static_cast<int64_t>(offsetKhz()) * 1000;
    const int64_t tx = static_cast<int64_t>(state.channel.rx_frequency)
                     + (offsetNeg_ ? -off : off);
    if ((tx < 0) || (tx > UINT32_MAX) || !freqInBand(static_cast<freq_t>(tx))) {
        vp_announceError(vp_getVoiceLevelQueueFlags());
        return;
    }

    state.channel.tx_frequency = static_cast<freq_t>(tx);
    requestSyncRtx();
    endEdit();
}

bool RadioView::onEditEvent(const Event &e)
{
    /* Direction and Step are plain Steppers: the base handles them. */
    if (editRow_ != RowOffset)
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
                /* Digits retype the value and '*' backspaces; the first digit
                 * replaces the pre-filled offset. */
                const bool digit = ((k & KBD_NUM_MASK) != 0u);
                if (digit || ((k & KEY_STAR) != 0u)) {
                    if (offsetPristine_) {
                        if (digit)
                            offsetInput_.clearSlots();
                        offsetPristine_ = false;
                    }
                    offsetInput_.handleKey(k, 0);
                    refreshCustom(RowOffset, true);
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
