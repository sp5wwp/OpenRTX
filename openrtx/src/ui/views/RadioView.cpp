/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/RadioView.hpp"
#include "core/Event.hpp"
#include "core/UiMemory.hpp"
#include "interfaces/keyboard.h"
#include "hwconfig.h"

#include <cstdio>
#include <cstring>

namespace ortxui
{

namespace
{

/* KEY_0..KEY_9 occupy bits 0..9, so a set digit bit's position is its value. */
constexpr uint32_t kDigitMask = 0x03FFu;

/* Format a frequency in Hz as a compact "N.NN kHz/MHz" (trailing zeros
 * stripped), e.g. 600000 -> "600 kHz", 6250 -> "6.25 kHz", 5000000 -> "5 MHz".
 * Sub-kHz values fall back to "N Hz". */
void formatFreq(uint32_t hz, char *buf, size_t size)
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
        sniprintf(buf, size, "%u Hz", (unsigned)hz);
        return;
    }

    char tmp[16];
    sniprintf(tmp, sizeof(tmp), "%u.%0*u", (unsigned)(hz / div), width,
              (unsigned)(hz % div));

    /* Strip trailing zeros, then a bare trailing dot. */
    size_t len = strlen(tmp);
    while ((len > 0) && (tmp[len - 1] == '0'))
        tmp[--len] = '\0';
    if ((len > 0) && (tmp[len - 1] == '.'))
        tmp[--len] = '\0';

    sniprintf(buf, size, "%s %cHz", tmp, prefix);
}

/* Lay a decimal value into a slot field left-aligned, so it reads back as that
 * integer via its filled prefix (filled slot count = number of digits). */
void setSlotsDecimal(TextInput &f, uint32_t val)
{
    char tmp[12];
    const int n = sniprintf(tmp, sizeof(tmp), "%u", (unsigned)val);
    f.clearSlots();
    const uint16_t cap = f.slotCount();
    uint16_t i = 0;
    for (; (i < (uint16_t)n) && (i < cap); i++)
        f.setSlotDigit(i, tmp[i] - '0');
    f.setCursorSlot(i);
}

} // namespace

void RadioView::build()
{
    static const UiText kLabels[RowCount] = {
        STR(offset),
        STR(direction),
        STR(step),
    };

    for (uint8_t i = 0; i < RowCount; i++) {
        items_[i].label = kLabels[i];
        items_[i].value = bufs_[i];
    }
    buildList(STR(radio), items_, RowCount);
    refreshCustom(RowOffset);
    refreshValue(RowDirection);
    refreshValue(RowStep);

    lastRx_ = state.channel.rx_frequency;
    lastTx_ = state.channel.tx_frequency;
    lastStep_ = state.step_index;
}

RadioView::RowKind RadioView::rowKind(uint8_t row) const
{
    return (row == RowOffset) ? RowKind::Custom : RowKind::Stepper;
}

void RadioView::formatValue(uint8_t row, char *out, size_t cap)
{
    /* Direction/Step are Steppers; Offset is Custom (see refreshCustom). */
    const channel_t &ch = state.channel;
    if (row == RowDirection) {
        if (cap >= 2u) {
            out[0] = (ch.tx_frequency >= ch.rx_frequency) ? '+' : '-';
            out[1] = '\0';
        }
    } else {
        formatFreq(freq_steps[state.step_index], out, cap);
    }
}

void RadioView::setValueText(uint8_t row, const char *s)
{
    sniprintf(bufs_[row], sizeof(bufs_[row]), "%s", s);
}

void RadioView::refreshCustom(uint8_t row)
{
    if (row != RowOffset)
        return;

    if (editing_ && (editRow_ == RowOffset)) {
        const unsigned v = (unsigned)offsetKhz();
        sniprintf(bufs_[RowOffset], sizeof(bufs_[RowOffset]), "<%u kHz>", v);
        char clean[16];
        sniprintf(clean, sizeof(clean), "%u kHz", v);
        vpSay(clean);
    } else {
        const channel_t &ch = state.channel;
        const uint32_t off = (ch.tx_frequency >= ch.rx_frequency) ?
                                 (ch.tx_frequency - ch.rx_frequency) :
                                 (ch.rx_frequency - ch.tx_frequency);
        char inner[16];
        formatFreq(off, inner, sizeof(inner));
        sniprintf(bufs_[RowOffset], sizeof(bufs_[RowOffset]), "%s", inner);
    }
    list_.invalidate();
}

void RadioView::onBeginEdit(uint8_t row)
{
    if (row != RowOffset)
        return;
    /* Pre-fill the keypad with the current offset (magnitude + direction) so
     * re-selecting and pressing ENTER preserves the split; the previous code
     * reset it to 0, which wiped the offset on re-entry. */
    const channel_t &ch = state.channel;
    offsetNeg_ = (ch.tx_frequency < ch.rx_frequency);
    const uint32_t mag = (offsetNeg_ ? ch.rx_frequency - ch.tx_frequency :
                                       ch.tx_frequency - ch.rx_frequency)
                       / 1000u;
    offsetInput_.configureSlots(offsetBuf_, sizeof(offsetBuf_), "999999", '-',
                                TextInput::CursorStyle::Bracket, FONT_SIZE_8PT);
    setSlotsDecimal(offsetInput_, mag);
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
    offsetInput_.configureSlots(offsetBuf_, sizeof(offsetBuf_), "999999", '-',
                                TextInput::CursorStyle::Bracket, FONT_SIZE_8PT);
    memcpy(offsetBuf_, e.text, sizeof(offsetBuf_));
    offsetInput_.setCursorSlot(e.cursor);
    offsetNeg_ = e.negative;
    offsetPristine_ = e.pristine;
    sniprintf(bufs_[RowOffset], sizeof(bufs_[RowOffset]), "<%u kHz>",
              (unsigned)offsetKhz());
    list_.invalidate();
}

void RadioView::onAdjust(uint8_t row, int dir)
{
    channel_t &ch = state.channel;

    if (row == RowDirection) {
        /* Mirror the TX split about RX: tx' = 2*rx - tx. A simplex channel
         * (tx == rx) is unaffected. */
        const int64_t rx = ch.rx_frequency;
        const int64_t tx = ch.tx_frequency;
        const int64_t mirrored = 2 * rx - tx;
        if (mirrored > 0) {
            ch.tx_frequency = (uint32_t)mirrored;
            lastTx_ = ch.tx_frequency;
            requestSyncRtx();
        }
    } else { /* RowStep: cycle the tuning step (no rtx effect) */
        const int n = (int)n_freq_steps;
        int s = ((int)state.step_index + dir) % n;
        if (s < 0)
            s += n;
        state.step_index = (uint8_t)s;
        lastStep_ = state.step_index;
    }
}

uint32_t RadioView::offsetKhz() const
{
    /* The filled slots read left-to-right as a decimal kHz value. */
    uint32_t v = 0;
    const uint16_t n = offsetInput_.filledSlots();
    for (uint16_t o = 0; o < n; o++)
        v = v * 10u + (uint32_t)offsetInput_.slotDigit(o);
    return v;
}

void RadioView::offsetAdjust(int dir)
{
    /* Nudge the offset magnitude by the current tuning step (integer kHz, min 1),
     * matching the Step field's knob behaviour; the keypad and '*' still work. */
    uint32_t stepKhz = freq_steps[state.step_index] / 1000u;
    if (stepKhz == 0u)
        stepKhz = 1u;

    uint32_t v = offsetKhz();
    if (dir > 0)
        v = (v + stepKhz > 999999u) ? 999999u : (v + stepKhz);
    else
        v = (v > stepKhz) ? (v - stepKhz) : 0u;

    setSlotsDecimal(offsetInput_, v);
    offsetPristine_ = true; /* the nudged value becomes a fresh pre-fill */
    refreshCustom(RowOffset);
}

void RadioView::offsetApply()
{
    /* Apply the entered magnitude in the current direction (keeping a '-' split
     * negative), so re-selecting + ENTER round-trips the exact offset. */
    const int64_t off = (int64_t)offsetKhz() * 1000;
    const int64_t tx = (int64_t)state.channel.rx_frequency
                     + (offsetNeg_ ? -off : off);
    if ((tx >= 0) && (tx <= 0xFFFFFFFF)) {
        state.channel.tx_frequency = (uint32_t)tx;
        lastTx_ = state.channel.tx_frequency;
        requestSyncRtx();
    }
    endEdit();
}

void RadioView::syncFromState(const state_t &s)
{
    View::syncFromState(s); /* top bar */

    const channel_t &ch = s.channel;
    bool changed = false;

    /* Offset + Direction both derive from the TX/RX pair. */
    if (!(editing_ && editRow_ == RowOffset)
        && ((ch.tx_frequency != lastTx_) || (ch.rx_frequency != lastRx_))) {
        refreshCustom(RowOffset);
        changed = true;
    }
    if (!(editing_ && editRow_ == RowDirection)
        && ((ch.tx_frequency != lastTx_) || (ch.rx_frequency != lastRx_))) {
        refreshValue(RowDirection);
        changed = true;
    }
    if ((ch.tx_frequency != lastTx_) || (ch.rx_frequency != lastRx_)) {
        lastTx_ = ch.tx_frequency;
        lastRx_ = ch.rx_frequency;
    }

    if (!(editing_ && editRow_ == RowStep) && (s.step_index != lastStep_)) {
        refreshValue(RowStep);
        lastStep_ = s.step_index;
        changed = true;
    }

    if (changed)
        list_.invalidate();
}

bool RadioView::onEditEvent(const Event &e)
{
    /* Only the Offset (Custom) row owns its editing stream; Direction/Step fall
     * through to the base's generic stepper handling. */
    if (editRow_ != RowOffset)
        return false;

    int dir = 0;
    if (e.kind == EvKind::Encoder) {
        dir = e.encoder; /* knob nudges the offset by the tuning step */
    } else if (e.kind == EvKind::KeyRepeat) {
        /* A held arrow keeps nudging; every other key acts once. */
        if ((e.keys & (KEY_UP | KEY_RIGHT)) != 0u)
            dir = +1;
        else if ((e.keys & (KEY_DOWN | KEY_LEFT)) != 0u)
            dir = -1;
    } else if (e.kind == EvKind::Key) {
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
            dir = +1; /* like the Step field */
        } else if ((k & (KEY_DOWN | KEY_LEFT)) != 0u) {
            dir = -1;
        } else {
            /* Digits retype the value, '*' backspaces -- through the shared key
             * contract, so delete is '*' here as everywhere. The pre-filled
             * value is pristine until the first digit clears it. */
            const bool digit = ((k & kDigitMask) != 0u);
            if (digit || ((k & KEY_STAR) != 0u)) {
                if (offsetPristine_) {
                    if (digit)
                        offsetInput_.clearSlots();
                    offsetPristine_ = false;
                }
                offsetInput_.handleKey(k, 0);
                refreshCustom(RowOffset);
            }
            return true;
        }
    }
    if (dir != 0)
        offsetAdjust(dir);
    return true;
}

} // namespace ortxui
