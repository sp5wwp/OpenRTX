/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/VfoView.hpp"
#include "style/Symbols.hpp"
#include "style/Charsets.hpp"
#include "core/Event.hpp"
#include "core/UiMemory.hpp"
#include "core/Layout.hpp"
#include "interfaces/delays.h"
#include "core/utils.h"
#include "core/cps.h"
#include "rtx/rtx.h"
#include "interfaces/keyboard.h"
#include "interfaces/platform.h"
#include "interfaces/cps_io.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"
#include "hwconfig.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace ortxui
{

/* Slot mask for keypad frequency entry: 7 editable digits, no separators
 * (FreqHero composes the MHz.kHz layout). Slot 0 = 100 MHz .. slot 6 = 100 Hz. */
static constexpr char kFreqMask[] = "9999999";

/* Slots of a frequency field, and what an empty one shows. */
static constexpr uint16_t kFreqSlots = sizeof(kFreqMask) - 1;
static constexpr char kPlaceholder = '-';

/* How long an out-of-band entry error stays on screen before it self-clears. */
static constexpr long long kInputErrorMs = 1500;

/* The TX power meter is a dB (log) scale relative to the radio's maximum
 * output. There is no per-device max-power field yet (hwInfo_t only carries
 * bands), so assume 5 W — the rating of essentially every OpenRTX-supported
 * radio; when a hwInfo max-power field lands, read it here instead. The meter
 * empties kPowerSpanDb below the max, so full power fills every dot and each
 * halving of power (~3 dB) drops a few dots. */
static constexpr uint32_t kMaxPowerMw = 5000;
static constexpr float kPowerSpanDb = 10.0f;

/**
 * Clamp a fraction to [0, 1].
 *
 * @param f: fraction.
 * @return the clamped fraction.
 */
static float clampFrac(float f)
{
    if (f < 0.0f)
        return 0.0f;
    if (f > 1.0f)
        return 1.0f;
    return f;
}

/**
 * Get the meter fill fraction for a TX power on the dB scale. Any nonzero
 * power shows at least a sliver (you are transmitting something).
 *
 * @param mw: power in mW.
 * @return the fraction, [0, 1].
 */
static float powerToFrac(uint32_t mw)
{
    if (mw == 0u)
        return 0.0f;
    const float db =
        10.0f
        * std::log10(static_cast<float>(mw) / static_cast<float>(kMaxPowerMw));
    return clampFrac(1.0f + db / kPowerSpanDb);
}

/**
 * Get the meter fill fraction for an RSSI, a continuous mirror of
 * rssiToSlevel() normalised to S11 (the meter's full scale, matching the
 * classic S-meter). By mapping in the dB domain the bar keeps full resolution
 * instead of snapping to whole S-points, so the squelch mark (fed the same
 * way) lands precisely.
 *
 * @param rssi: RSSI in dBm.
 * @return the fraction, [0, 1].
 */
static float rssiToFrac(float rssi)
{
    /* S1..S9 rise 6 dB/point (-121..-73), S9..S11 rise 10 dB/point (-73..-53). */
    float s;
    if (rssi <= -121.0f)
        s = 0.0f;
    else if (rssi < -73.0f)
        s = (127.0f + rssi) / 6.0f;
    else if (rssi < -53.0f)
        s = (163.0f + rssi) / 10.0f;
    else
        s = 11.0f;
    return clampFrac(s / 11.0f);
}

/**
 * Get the digit of a bare number key (KEY_0..KEY_9 are bits 0..9).
 *
 * @param keys: keyboard bitmask.
 * @return the lowest digit pressed, -1 for none.
 */
static int digitFromKeys(uint32_t keys)
{
    const uint32_t digits = keys & 0x3FFu;
    if (digits == 0u)
        return -1;
    return __builtin_ctz(digits);
}

/**
 * Get the place value of a slot in a frequency field: the last slot is 100 Hz,
 * each earlier slot ten times larger (slot 0 of 7 = 100 MHz).
 *
 * @param o: slot ordinal.
 * @param n: slots in the field.
 * @return the place value in Hz.
 */
static freq_t slotCoeff(uint16_t o, uint16_t n)
{
    freq_t c = 100;
    for (uint16_t i = 0; i + 1 + o < n; i++)
        c *= 10;
    return c;
}

/**
 * Get the digit in a slot of a field's buffer. The mask has no separators, so
 * slot `o` is character `o`.
 *
 * @param digits: field buffer.
 * @param o: slot ordinal.
 * @return the digit, -1 if the slot is empty.
 */
static int slotDigit(const char *digits, uint16_t o)
{
    return (digits[o] != kPlaceholder) ? (digits[o] - '0') : -1;
}

/**
 * Read a slot field back as a frequency. Un-entered (placeholder) slots
 * contribute 0, so a partial entry keeps its automatic trailing zeros.
 *
 * @param digits: field buffer.
 * @return the frequency in Hz.
 */
static freq_t freqFromSlots(const char *digits)
{
    freq_t v = 0;
    for (uint16_t o = 0; o < kFreqSlots; o++) {
        const int d = slotDigit(digits, o);
        if (d >= 0)
            v += (freq_t)d * slotCoeff(o, kFreqSlots);
    }
    return v;
}

/**
 * Fill a slot field from a frequency, snapping to the field's 100 Hz grid.
 *
 * @param digits: field buffer.
 * @param hz: frequency in Hz.
 */
static void slotsFromFreq(char *digits, freq_t hz)
{
    for (uint16_t o = 0; o < kFreqSlots; o++)
        digits[o] =
            static_cast<char>('0' + (hz / slotCoeff(o, kFreqSlots)) % 10);
}

/**
 * Compose the FreqHero entry strings from a 7-slot field: "MMM.KKK" main and a
 * 2-digit sub (the 100 Hz slot + a fixed 10 Hz zero), with '-' for un-entered
 * slots so the operator sees exactly how many digits remain.
 *
 * @param digits: field buffer.
 * @param main: main text output, 8 bytes.
 * @param sub: sub text output, 3 bytes.
 */
static void composeEntry(const char *digits, char *main, char *sub)
{
    auto sc = [&](uint16_t o) -> char {
        const int d = slotDigit(digits, o);
        return (d >= 0) ? static_cast<char>('0' + d) : '-';
    };
    main[0] = sc(0);
    main[1] = sc(1);
    main[2] = sc(2);
    main[3] = '.';
    main[4] = sc(3);
    main[5] = sc(4);
    main[6] = sc(5);
    main[7] = '\0';

    const int d6 = slotDigit(digits, 6);
    sub[0] = (d6 >= 0) ? static_cast<char>('0' + d6) : '-';
    sub[1] = (d6 >= 0) ? '0' : '-';
    sub[2] = '\0';
}

/**
 * Check whether a frequency falls inside one of the radio's supported bands
 * (ported from the classic _ui_freq_check_limits).
 *
 * @param freq: frequency in Hz.
 * @return true if in band.
 */
static bool freqInBand(freq_t freq)
{
    const hwInfo_t *hw = platform_getHwInfo();
    if (hw->vhf_band && (freq >= (freq_t)hw->vhf_minFreq * 1000000u)
        && (freq <= (freq_t)hw->vhf_maxFreq * 1000000u))
        return true;
    if (hw->uhf_band && (freq >= (freq_t)hw->uhf_minFreq * 1000000u)
        && (freq <= (freq_t)hw->uhf_maxFreq * 1000000u))
        return true;
    return false;
}

VfoView::VfoView()
{
}

VfoView::~VfoView()
{
    if (entry_ != nullptr)
        closeEntry();
}

void VfoView::build()
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;
    const bool regular = (sizeClass() == SizeClass::Regular);

    root_.setArea({ 0, 0, (uint16_t)W, (uint16_t)H });
    root_.setAxis(Axis::Column);

    /* Shared top bar with a blank title (VFO shows only the status cluster). */
    TopBar &bar = useTopBar("");

    /* Frequency hero. On the roomy screen it is a little taller than the text
     * needs so the mode stack breathes; on the tiny mono screen it is trimmed
     * to just clear the frequency and the mode/tone stack, freeing vertical
     * room below so the channel line and meter both fit within 64px. */
    hero_.setBasis(regular ? 40 : 26);

    /* Channel identity, all one text size. The compact screen uses a smaller
     * font so a full line (ascent + descent) still fits its tight budget. */
    const fontSize_t chanFont = regular ? FONT_SIZE_8PT : FONT_SIZE_6PT;
    /* Row height = the font's full line height (ascent + descent) so descenders
     * (and the '@' / caps of an M17 destination) are not clipped. */
    const int16_t rowH = gfx_getFontLineHeight(chanFont);

    /* Index / "where am I" slot: "VFO" or the 1-based memory index. Wide enough
     * for the widest content ("VFO" ~30px @ 8pt, ~23px @ 6pt). */
    chanIdx_.setFont(chanFont);
    chanIdx_.setAlign(TEXT_ALIGN_LEFT);
    chanIdx_.setColor(Sem::OnSurfaceMuted);
    chanIdx_.setBasis(regular ? 34 : 26);

    /* Channel name / M17 destination, in blue -- the "who / where" headline. On
     * the roomy screen it gets its OWN full-width line, so a long destination
     * ("@HELLOWORLD") shows in full instead of ellipsizing in the sliver left
     * between the index and the CAN tag. */
    chanName_.setFont(chanFont);
    chanName_.setAlign(TEXT_ALIGN_LEFT);
    chanName_.setColor(Sem::Primary);
    chanName_.setGrow(1);
    /* Still ellipsize as a backstop (a pathological name wider than the whole
     * line) rather than wrapping into the row below. */
    chanName_.setOverflow(Label::Overflow::Ellipsize);

    /* Detail = a small "CAN" tag + the value at the row font (or just a tone
     * value in FM), right-aligned and sharing one baseline. */
    chanDetail_.setRun(0, FONT_SIZE_5PT, Sem::OnSurfaceMuted); /* tag */
    chanDetail_.setRun(1, chanFont, Sem::OnSurfaceMuted);      /* value */
    chanDetail_.setGap(3);
    /* Wide enough for the longest tagged value -- "CTCSS 254.1" -- so the tag
     * is never clipped; the roomy meta strip has space to spare, and the M17
     * "CAN n" simply right-aligns within it. */
    chanDetail_.setBasis(regular ? 70 : 48);

    /* Signal meter, pinned to the bottom of the screen: the growing spacer sits
     * ABOVE it, pushing the readout down and letting the identity block breathe
     * near the top. */
    /* Tall enough that the label/readout (6 pt) clear the bar band above the
     * S-scale numbers (5 pt) without the ascenders clipping. */
    meter_.setBasis(regular ? 19 : 15);
    meter_.setColor(Sem::RxSuccess);
    meter_.setLabel("RX");
    spacer_.setGrow(1);

    root_.addChild(&bar);
    root_.addChild(&hero_);

    if (regular) {
        /* Roomy layout: the destination is the headline on its own full-width
         * line; below it a quieter meta strip carries the index (left) and the
         * CAN / tone detail (right, under the mode label in the hero). */
        nameRow_.setAxis(Axis::Row);
        nameRow_.setAlign(Align::Center);
        nameRow_.setJustify(Justify::Start);
        nameRow_.setPadding(4, 0);
        nameRow_.setBasis(rowH);
        nameRow_.addChild(&chanName_);

        metaRow_.setAxis(Axis::Row);
        metaRow_.setAlign(Align::Center);
        metaRow_.setJustify(Justify::SpaceBetween);
        metaRow_.setPadding(4, 0);
        metaRow_.setBasis(rowH);
        metaRow_.addChild(&chanIdx_);
        metaRow_.addChild(&chanDetail_);

        root_.addChild(&nameRow_);
        root_.addChild(&metaRow_);
    } else {
        /* Compact layout: no room for a second line, so the index, name and
         * detail share one row (the name ellipsizes) as before. */
        nameRow_.setAxis(Axis::Row);
        nameRow_.setAlign(Align::Center);
        nameRow_.setJustify(Justify::Start);
        nameRow_.setPadding(4, 0);
        nameRow_.setGap(6);
        nameRow_.setBasis(rowH);
        nameRow_.addChild(&chanIdx_);
        nameRow_.addChild(&chanName_);
        nameRow_.addChild(&chanDetail_);

        root_.addChild(&nameRow_);
    }

    root_.addChild(&spacer_);
    root_.addChild(&meter_);
    screen_.addChild(&root_);
    root_.onLayout();

    screen_.markAllDirty();
}

void VfoView::save(UiMemory &m) const
{
    InlineEdit &ie = m.inlineEdit;
    if (entry_ == nullptr) {
        if (ie.owner == id())
            ie.owner = ViewId::Count;
        return;
    }

    const Entry &n = *entry_;
    VfoEdit &e = ie.vfo;
    e.input = !n.destination;
    e.destination = n.destination;
    e.txField = n.txField;
    e.rxPristine = n.rxPristine;
    e.txPristine = n.txPristine;
    e.shift = static_cast<int32_t>(n.shift);
    e.errorAt = n.errorAt;
    if (n.destination) {
        memcpy(e.text[0], n.dst, sizeof(n.dst));
        e.cursor[0] = static_cast<uint8_t>(n.input.cursor());
    } else {
        const uint8_t slot = static_cast<uint8_t>(n.input.cursorSlot());
        memcpy(e.text[0], n.rx, sizeof(n.rx));
        memcpy(e.text[1], n.tx, sizeof(n.tx));
        e.cursor[0] = n.txField ? n.rxSlot : slot;
        e.cursor[1] = n.txField ? slot : n.txSlot;
    }
    ie.owner = id();
}

void VfoView::restore(UiMemory &m)
{
    InlineEdit &ie = m.inlineEdit;
    if (ie.owner != id())
        return;
    ie.owner = ViewId::Count;

    const VfoEdit &e = ie.vfo;
    if (!openEntry(e.destination))
        return;

    Entry &n = *entry_;
    if (e.destination) {
        memcpy(n.dst, e.text[0], sizeof(n.dst));
        n.input.configure(n.dst, sizeof(n.dst), CHARSET_CALLSIGN,
                          TextInput::Mode::SingleLine, FONT_SIZE_8PT);
        n.input.setMultiTap(MTAP_CALLSIGN);
        n.input.resume(e.cursor[0], 0);
        renderDstEdit();
        return;
    }

    memcpy(n.rx, e.text[0], sizeof(n.rx));
    memcpy(n.tx, e.text[1], sizeof(n.tx));
    n.rxSlot = e.cursor[0];
    n.txSlot = e.cursor[1];
    bindField(e.txField, e.txField ? n.txSlot : n.rxSlot);
    n.txField = e.txField;
    n.rxPristine = e.rxPristine;
    n.txPristine = e.txPristine;
    n.shift = e.shift;
    n.errorAt = e.errorAt;
    refreshInput();
}

bool VfoView::openEntry(bool destination)
{
    void *mem = malloc(sizeof(Entry));
    if (mem == nullptr)
        return false;

    entry_ = new (mem) Entry();
    entry_->destination = destination;
    return true;
}

void VfoView::closeEntry()
{
    entry_->~Entry();
    free(entry_);
    entry_ = nullptr;
}

void VfoView::syncMode(const channel_t &ch)
{
    const char *m1 = "--";
    switch (ch.mode) {
        case OPMODE_FM:
            m1 = (ch.bandwidth == BW_25) ? "WFM" : "NFM";
            break;
        case OPMODE_DMR:
            m1 = "DMR";
            break;
        case OPMODE_M17:
            m1 = SYMBOL_M17; /* the M17 wordmark logo glyph */
            break;
        default:
            break;
    }

    /* Channel-row detail (right, under the mode): a small "CAN" tag + number in
     * M17 -- but only when the CAN is actually gating traffic: on TX (you key
     * up on it) or when RX is filtered to it (m17_can_rx). While RX is
     * promiscuous and idle the CAN filters nothing, so hide it. In FM it is the
     * CTCSS tone, tagged "CTCSS" (small, like the CAN tag), matching the current
     * direction (the TX/encode tone while transmitting, else the RX/decode
     * tone), shown only when that direction's tone is enabled. Blank
     * otherwise. */
    const char *tag = "";
    char sub[sizeof(modeSub_)] = "";
    if (ch.mode == OPMODE_M17) {
        const bool txing = (rtx_getStatus()->opStatus == TX);
        if (txing || state.settings.m17_can_rx) {
            tag = "CAN";
            sniprintf(sub, sizeof(sub), "%u", (unsigned)state.settings.m17_can);
        }
    } else if (ch.mode == OPMODE_FM) {
        const bool txing = (rtx_getStatus()->opStatus == TX);
        const bool showTx = txing && (ch.fm.txToneEn != 0u);
        const bool showRx = !txing && (ch.fm.rxToneEn != 0u);
        if (showTx || showRx) {
            const uint8_t idx = showTx ? ch.fm.txTone : ch.fm.rxTone;
            const uint16_t t = ctcss_tone[idx];
            tag = "CTCSS";
            sniprintf(sub, sizeof(sub), "%u.%u", (unsigned)(t / 10),
                      (unsigned)(t % 10));
        }
    }

    /* Repaint only when the visible pair changes: covers mode/bandwidth, the
     * FM tone, and the dynamic M17 CAN (settings + TX/RX). The mode label sits
     * on the frequency baseline (hero); the detail sits under it on the channel
     * row. The texts shown are literals, so their pointers stay valid. */
    const bool same = (shownMode_ != nullptr) && (strcmp(m1, shownMode_) == 0)
                   && (strcmp(tag, shownTag_) == 0)
                   && (strcmp(sub, modeSub_) == 0);
    if (same)
        return;
    shownMode_ = m1;
    shownTag_ = tag;
    memcpy(modeSub_, sub, sizeof(modeSub_));

    hero_.setMode(m1);
    hero_.invalidate();
    chanDetail_.setText(0, tag);
    chanDetail_.setText(1, modeSub_);
    chanDetail_.invalidate();
}

void VfoView::syncMeter(const state_t &s)
{
    /* Live TX/RX comes from the rtx module (state.rtxStatus is only the
     * requested mode); this matches how the classic UI reads it. */
    const uint8_t status = rtx_getStatus()->opStatus;
    const uint8_t sql = s.settings.sqlLevel;
    const uint8_t mode = s.channel.mode;
    const bool changed = (status != lastStatus_) || (s.rssi != lastRssi_)
                      || (sql != lastSql_) || (mode != lastMode_);
    if (!changed)
        return;

    if (status == TX) {
        const uint32_t mw = s.channel.power;

        meter_.setLabel("TX");
        meter_.setColor(Sem::TxDanger);
        meter_.setShowScale(false);
        meter_.setMarker(-1.0f); /* squelch is an RX/FM concept */
        /* Fill on a dB power scale relative to the radio's max output. */
        meter_.setValue(powerToFrac(mw));

        if (mw >= 1000u)
            sniprintf(readoutBuf_, sizeof(readoutBuf_), "%lu.%luW",
                      (unsigned long)(mw / 1000u),
                      (unsigned long)((mw % 1000u) / 100u));
        else
            sniprintf(readoutBuf_, sizeof(readoutBuf_), "%lumW",
                      (unsigned long)mw);
    } else {
        const uint8_t level = rssiToSlevel(s.rssi);

        meter_.setLabel("RX");
        meter_.setColor(Sem::RxSuccess);
        meter_.setShowScale(true);
        meter_.setValue(rssiToFrac(static_cast<float>(s.rssi)));

        /* FM squelch opens at squelch_rssi = -127 + sqlLevel*66/15 dBm (see
         * OpMode_FM); feed it through the same continuous RSSI->fraction map as
         * the fill so the red gate sits exactly where the signal has to reach.
         * Only FM RF-gates on RSSI, so the mark is hidden in the digital modes. */
        if (mode == OPMODE_FM)
            meter_.setMarker(rssiToFrac(-127.0f + (sql * 66.0f) / 15.0f));
        else
            meter_.setMarker(-1.0f);

        if (level > 9u)
            sniprintf(readoutBuf_, sizeof(readoutBuf_), "+%u",
                      (unsigned)((level - 9u) * 10u));
        else
            sniprintf(readoutBuf_, sizeof(readoutBuf_), "S%u", (unsigned)level);
    }

    meter_.setReadout(readoutBuf_);
    meter_.invalidate();

    lastStatus_ = status;
    lastRssi_ = s.rssi;
    lastSql_ = sql;
    lastMode_ = mode;
}

void VfoView::syncFromState(const state_t &s)
{
    View::syncFromState(s); /* shared top bar */

    /* While entering a frequency, the hero and channel line show the keypad
     * buffer (driven by refreshInput()); don't let live state overwrite them. */
    const bool input = (entry_ != nullptr) && !entry_->destination;
    if (!input) {
        const channel_t &ch = s.channel;

        /* While actually transmitting a channel with an RX/TX split (repeater
         * offset), show the TX frequency the radio is keyed up on; otherwise
         * (idle, or simplex) show the RX frequency. Gating on the *shown* value
         * repaints on both frequency changes and TX start/stop. */
        const bool txing = (rtx_getStatus()->opStatus == TX);
        const freq_t shown = (txing && (ch.tx_frequency != ch.rx_frequency)) ?
                                 ch.tx_frequency :
                                 ch.rx_frequency;
        if ((uint32_t)shown != lastFreq_) {
            hero_.setFreq((uint32_t)shown);
            hero_.invalidate();
            lastFreq_ = (uint32_t)shown;
        }

        syncMode(ch);

        /* Channel line (skipped while editing the destination, which drives it
         * directly): VFO mode shows "VFO" (or the M17 destination) with no
         * index; memory mode shows the 1-based index + channel name. Compose
         * the slot text and repaint only on a string change. */
        if (entry_ == nullptr) {
            char idxText[8];
            char nameText[40];
            composeChanLine(s, idxText, nameText, sizeof(nameText));

            if (strncmp(idxText, idxCache_, sizeof(idxCache_)) != 0) {
                strncpy(idxCache_, idxText, sizeof(idxCache_) - 1);
                idxCache_[sizeof(idxCache_) - 1] = '\0';
                chanIdx_.setText(idxCache_);
                chanIdx_.invalidate();
            }
            if (strncmp(nameText, nameCache_, sizeof(nameCache_)) != 0) {
                strncpy(nameCache_, nameText, sizeof(nameCache_) - 1);
                nameCache_[sizeof(nameCache_) - 1] = '\0';
                chanName_.setText(nameCache_);
                chanName_.invalidate();
            }
        }
    } else if (entry_->errorAt != 0) {
        /* Expire a transient out-of-band error (this runs every GUI frame). */
        if ((getTick() - entry_->errorAt) >= kInputErrorMs) {
            entry_->errorAt = 0;
            refreshInput();
        }
    }

    syncMeter(s);
}

NavIntent VfoView::onEvent(const Event &e)
{
    if (entry_ != nullptr)
        return entry_->destination ? onDstEditEvent(e) : onInputEvent(e);

    /* The knob tunes in VFO mode and steps channels in memory mode. */
    if (e.kind == EvKind::Encoder) {
        clearFmTone(); /* any input other than # drops the momentary tone */
        step(e.encoder);
        return NavIntent::none();
    }

    /* A held arrow keeps tuning; every other key acts once. */
    if (e.kind == EvKind::KeyRepeat) {
        if ((e.keys & KEY_UP) != 0u)
            step(+1);
        else if ((e.keys & KEY_DOWN) != 0u)
            step(-1);
        return NavIntent::none();
    }

    if (e.kind == EvKind::Key) {
        const uint32_t k = e.keys;

        /* The momentary FM tone latches on # and is cleared by the next other
         * key (classic parity). */
        if ((k & KEY_HASH) == 0u)
            clearFmTone();

        if ((k & KEY_ENTER) != 0u) {
            return NavIntent::push(ViewId::MainMenu);
        } else if ((k & KEY_ESC) != 0u) {
            toggleVfoMem();
        } else if ((k & KEY_HASH) != 0u) {
            /* # opens the M17 destination editor, or keys the FM tone burst. */
            if (state.channel.mode == OPMODE_M17) {
                beginDstEdit();
            } else if ((state.channel.mode == OPMODE_FM)
                       && !state.tone_enabled) {
                state.tone_enabled = true;
                requestSyncRtx();
            }
        } else if ((k & KEY_UP) != 0u) {
            step(+1);
        } else if ((k & KEY_DOWN) != 0u) {
            step(-1);
        } else if ((k & KEY_STAR) != 0u) {
            /* '*' opens the keypad on the current frequency for an in-place
             * edit (backspace the trailing kHz); a digit retypes from scratch. */
            if (state.tuner_mode == VFO)
                beginInputTweak();
        } else if (state.tuner_mode == VFO) {
            /* A digit opens the frequency keypad (VFO mode only). */
            const int d = digitFromKeys(k);
            if (d >= 0)
                beginInput((uint8_t)d);
        }
        return NavIntent::none();
    }

    screen_.dispatch(e);
    return NavIntent::none();
}

NavIntent VfoView::onInputEvent(const Event &e)
{
    if (e.kind != EvKind::Key)
        return NavIntent::none();

    const uint32_t k = e.keys;
    if ((k & KEY_ENTER) != 0u) {
        confirmInput();
        return NavIntent::none();
    }
    if ((k & KEY_ESC) != 0u) {
        exitInput(); /* discard the entry */
        return NavIntent::none();
    }
    if ((k & (KEY_UP | KEY_DOWN)) != 0u) {
        switchField(); /* toggle RX/TX, deriving TX from RX + offset */
        return NavIntent::none();
    }

    /* Character-level keys go through the shared TextInput key contract, so '*'
     * is backspace here exactly as in every other editor. Only act on keys that
     * actually edit the field (a digit, '*' backspace, or a cursor move); ignore
     * key-release (k==0) and other keys so they can't silently consume the
     * pristine (pre-filled) state without editing it. */
    const int digit = digitFromKeys(k);
    const bool mutating = (digit >= 0) || ((k & KEY_STAR) != 0u)
                       || ((k & (KEY_LEFT | KEY_RIGHT)) != 0u);
    if (!mutating)
        return NavIntent::none();

    Entry &n = *entry_;
    TextInput &f = n.input;
    bool &pristine = n.txField ? n.txPristine : n.rxPristine;
    if (pristine) {
        if (digit >= 0)
            f.clearSlots(); /* first digit retypes the whole frequency */
        pristine = false;   /* '*' / arrows instead begin an in-place tweak */
    }
    f.handleKey(k, getTick()); /* a known-mutating key always edits the field */

    n.errorAt = 0;
    if ((digit >= 0) && (state.settings.vpLevel >= vpLow)) {
        vp_flush();
        vp_queueInteger(digit);
        vp_play();
    }
    refreshInput();
    return NavIntent::none();
}

void VfoView::step(int dir)
{
    if (state.tuner_mode == VFO)
        stepFreq(dir);
    else
        stepChannel(dir);
}

void VfoView::stepFreq(int dir)
{
    const freq_t step = freq_steps[state.step_index];
    freq_t rx = state.channel.rx_frequency;
    freq_t tx = state.channel.tx_frequency;
    const freq_t nrx = (dir > 0) ? (rx + step) : (rx - step);
    const freq_t ntx = (dir > 0) ? (tx + step) : (tx - step);

    /* Reject a step that would push either edge out of band (underflow wraps
     * to a huge value and is rejected the same way). */
    if (!freqInBand(nrx) || !freqInBand(ntx))
        return;

    state.channel.rx_frequency = nrx;
    state.channel.tx_frequency = ntx;
    requestSyncRtx();
    announceFreq();
}

void VfoView::stepChannel(int dir)
{
    if (loadChannel((int16_t)((int)state.channel_index + dir))) {
        requestSyncRtx();
        if (state.settings.vpLevel >= vpLow)
            vp_announceChannelName(&state.channel,
                                   (uint16_t)(state.channel_index + 1),
                                   vp_getVoiceLevelQueueFlags());
    }
}

void VfoView::toggleVfoMem()
{
    if (state.tuner_mode == VFO) {
        /* VFO -> memory: remember the VFO, then load the current channel. */
        state.vfo_channel = state.channel;
        if (loadChannel((int16_t)state.channel_index)) {
            state.tuner_mode = CH;
            requestSyncRtx();
            if (state.settings.vpLevel >= vpLow)
                vp_announceChannelName(&state.channel,
                                       (uint16_t)(state.channel_index + 1),
                                       vp_getVoiceLevelQueueFlags());
        }
        /* An empty/invalid codeplug leaves us in VFO mode (no channel). */
    } else {
        /* Memory -> VFO: restore the saved VFO channel. */
        state.channel = state.vfo_channel;
        state.tuner_mode = VFO;
        requestSyncRtx();
        announceFreq();
    }
}

bool VfoView::loadChannel(int16_t index)
{
    const int16_t selected = index;
    int16_t readIndex = index;

    if (state.bank_enabled) {
        bankHdr_t bank = {};
        cps_readBankHeader(&bank, state.bank);
        if ((index < 0) || (index >= (int16_t)bank.ch_count))
            return false;
        readIndex = (int16_t)cps_readBankData(state.bank, (uint16_t)index);
    } else if (index < 0) {
        return false;
    }

    channel_t ch;
    if (cps_readChannel(&ch, (uint16_t)readIndex) != 0)
        return false;

    state.channel = ch;
    state.channel_index = (uint16_t)selected;
    return true;
}

bool VfoView::openInput()
{
    if (!openEntry(false))
        return false;

    Entry &n = *entry_;
    const channel_t &ch = state.channel;
    n.shift = (int64_t)ch.tx_frequency - (int64_t)ch.rx_frequency;

    /* TX starts empty; RX is fully pre-filled with the cursor at the end. TX
     * is derived lazily on the first switch / commit. */
    memset(n.tx, kPlaceholder, kFreqSlots);
    n.tx[kFreqSlots] = '\0';
    slotsFromFreq(n.rx, ch.rx_frequency);
    n.rx[kFreqSlots] = '\0';
    bindField(false, kFreqSlots);
    return true;
}

void VfoView::beginInput(uint8_t firstDigit)
{
    if (!openInput())
        return;

    /* First digit retypes from the top (the pre-filled RX is pristine). */
    entry_->rxPristine = false;
    entry_->input.clearSlots();
    entry_->input.putDigit(firstDigit);
    if (state.settings.vpLevel >= vpLow) {
        vp_flush();
        vp_queueInteger(firstDigit);
        vp_play();
    }
    refreshInput();
}

void VfoView::beginInputTweak()
{
    /* '*' opens the keypad on the full current frequency, cursor at the last
     * digit. The field stays pristine: the next '*' backspaces the trailing kHz
     * to correct in place, while a digit retypes the whole value from the top. */
    if (openInput())
        refreshInput();
}

void VfoView::switchField()
{
    Entry &n = *entry_;
    if (!n.txField && n.txPristine)
        deriveTx(); /* refresh TX from the latest RX + offset before showing it */

    /* Keep the cursor of the field left, and edit the other one. */
    const uint8_t slot = static_cast<uint8_t>(n.input.cursorSlot());
    if (n.txField)
        n.txSlot = slot;
    else
        n.rxSlot = slot;
    n.txField = !n.txField;
    bindField(n.txField, n.txField ? n.txSlot : n.rxSlot);
    n.errorAt = 0;
    refreshInput();
    if (state.settings.vpLevel >= vpLow)
        vp_announceInputReceiveOrTransmit(n.txField, vpqDefault);
}

void VfoView::deriveTx()
{
    Entry &n = *entry_;
    const freq_t rx = freqFromSlots(n.rx);
    int64_t tx = (int64_t)rx + n.shift;
    if (tx < 0)
        tx = 0; /* an underflowing offset is rejected in band at commit */
    slotsFromFreq(n.tx, (freq_t)tx);
    n.txSlot = kFreqSlots;
}

void VfoView::bindField(bool tx, uint8_t slot)
{
    /* Binding lays the mask over the buffer: put the digits back after. */
    Entry &n = *entry_;
    char *buf = tx ? n.tx : n.rx;
    char digits[kFreqDigits + 1];
    memcpy(digits, buf, sizeof(digits));
    n.input.configureSlots(buf, kFreqDigits + 1, kFreqMask, kPlaceholder,
                           FONT_SIZE_8PT);
    memcpy(buf, digits, sizeof(digits));
    n.input.setCursorSlot(slot);
}

void VfoView::confirmInput()
{
    /* ENTER on the RX field advances to TX (deriving it from the offset) so the
     * operator sees and confirms the TX frequency before anything is applied,
     * instead of silently committing on the first ENTER. ENTER on TX commits. */
    Entry &n = *entry_;
    if (!n.txField) {
        switchField();
        return;
    }

    const freq_t rx = freqFromSlots(n.rx);
    const freq_t tx = freqFromSlots(n.tx);
    if (freqInBand(rx) && freqInBand(tx)) {
        state.channel.rx_frequency = rx;
        state.channel.tx_frequency = tx;
        requestSyncRtx();
        if (state.settings.vpLevel >= vpLow) {
            vp_flush();
            vp_announceFrequencies(rx, tx, vpqDefault);
            vp_play();
        }
        exitInput();
    } else {
        /* Keep the buffer and flag the error; syncFromState clears it shortly,
         * or the next edit does. */
        n.errorAt = getTick();
        refreshInput();
    }
}

void VfoView::exitInput()
{
    closeEntry();

    /* Force the change-gated readout to repaint from live state next sync. */
    lastFreq_ = 0xFFFFFFFFu;
    shownMode_ = nullptr;
    idxCache_[0] = '\1';
    nameCache_[0] = '\1';
    screen_.markAllDirty();
}

void VfoView::refreshInput()
{
    const Entry &n = *entry_;
    char main[16];
    char sub[6];
    composeEntry(n.txField ? n.tx : n.rx, main, sub);

    const bool err = (n.errorAt != 0);
    hero_.setEntry(main, sub);
    hero_.setMode(n.txField ? "TX" : "RX");
    hero_.setTextColor(err ? Sem::Mark : Sem::Accent);
    hero_.invalidate();

    chanIdx_.setText("");
    chanName_.setText(err ? STR(outOfBand) :
                            (n.txField ? STR(enterTx) : STR(enterRx)));
    chanDetail_.setText(0, "");
    chanDetail_.setText(1, "");
    chanIdx_.invalidate();
    chanName_.invalidate();
    chanDetail_.invalidate();
}

void VfoView::clearFmTone()
{
    if (state.tone_enabled) {
        state.tone_enabled = false;
        requestSyncRtx();
    }
}

void VfoView::m17DstLabel(char *out, uint16_t sz)
{
    const char *dst = state.settings.m17_dest;
    if (dst[0] == '\0')
        sniprintf(out, sz, "@ALL");
    else
        sniprintf(out, sz, "@%s", dst);
}

void VfoView::composeChanLine(const state_t &s, char *idxOut, char *nameOut,
                              uint16_t nameSz)
{
    const channel_t &ch = s.channel;

    if (s.tuner_mode == VFO) {
        /* Far-left is the "where am I" slot: "VFO" here, the memory index in
         * memory mode. In M17 the destination fills the name slot; FM has no
         * name (just the frequency). The CAN lives in the mode stack, not here. */
        sniprintf(idxOut, 8, "VFO");
        if (ch.mode == OPMODE_M17)
            m17DstLabel(nameOut, nameSz);
        else
            nameOut[0] = '\0';
    } else {
        sniprintf(idxOut, 8, "%03u", s.channel_index + 1);
        sniprintf(nameOut, nameSz, "%s",
                  (ch.name[0] != '\0') ? ch.name : "---");
    }
}

void VfoView::renderDstEdit()
{
    /* Compose "@W1[A]W" with the cursor character in brackets, straight into
     * the channel-line name slot (no custom cursor rendering needed). The '@'
     * marks it as an M17 destination address and stays pinned while the
     * callsign body scrolls within the remaining width -- a leading/trailing
     * ellipsis marks where it is truncated, so the cursor is always visible. */
    const bool regular = (sizeClass() == SizeClass::Regular);
    const fontSize_t font = regular ? FONT_SIZE_10PT : FONT_SIZE_6PT;
    const uint16_t atW = gfx_getTextWidth(font, "@");
    const uint16_t boxW = chanName_.area().w;
    const uint16_t bodyW = (boxW > atW) ? static_cast<uint16_t>(boxW - atW) : 0;

    char body[36];
    entry_->input.formatWindow(body, sizeof(body), bodyW, font);
    sniprintf(nameCache_, sizeof(nameCache_), "@%s", body);
    idxCache_[0] = '\0';
    chanIdx_.setText(idxCache_);
    chanName_.setText(nameCache_);
    chanIdx_.invalidate();
    chanName_.invalidate();
}

void VfoView::announceDstChar()
{
    if (state.settings.vpLevel >= vpLow)
        vp_announceInputChar(entry_->input.cursorChar());
}

void VfoView::beginDstEdit()
{
    if (!openEntry(true))
        return;

    Entry &n = *entry_;
    strncpy(n.dst, state.settings.m17_dest, sizeof(n.dst) - 1);
    n.dst[sizeof(n.dst) - 1] = '\0';
    n.input.configure(n.dst, sizeof(n.dst), CHARSET_CALLSIGN,
                      TextInput::Mode::SingleLine, FONT_SIZE_8PT);
    n.input.setMultiTap(MTAP_CALLSIGN);
    n.input.begin();
    /* The channel-name font stays at its display size: renderDstEdit() scrolls
     * a cursor-visible window of the "@W1[A]W" string within the box instead of
     * shrinking the whole field to make a full-length destination fit. */
    renderDstEdit();
    screen_.markAllDirty();
    announceDstChar();
}

void VfoView::dstCycle(int dir)
{
    entry_->input.cycle(dir);
    renderDstEdit();
    announceDstChar();
}

void VfoView::dstMove(int dir)
{
    entry_->input.moveCursor(dir);
    renderDstEdit();
    announceDstChar();
}

void VfoView::endDstEdit(bool commit)
{
    if (commit) {
        /* Strip trailing spaces, then store the destination. */
        entry_->input.stripTrailingSpaces();
        strncpy(state.settings.m17_dest, entry_->dst,
                sizeof(state.settings.m17_dest) - 1);
        state.settings.m17_dest[sizeof(state.settings.m17_dest) - 1] = '\0';
        requestSyncRtx(); /* the destination feeds the M17 LSF */
    }

    closeEntry();
    /* Force the channel line to recompose from live state next sync. */
    idxCache_[0] = '\1';
    nameCache_[0] = '\1';
    screen_.markAllDirty();
}

NavIntent VfoView::onDstEditEvent(const Event &e)
{
    if (e.kind == EvKind::Encoder) {
        /* Knob moves the cursor on arrow-less radios, cycles elsewhere. */
        if (kbdHasArrows())
            dstCycle(e.encoder);
        else
            dstMove(e.encoder > 0 ? +1 : -1);
    } else if (e.kind == EvKind::Key) {
        const uint32_t k = e.keys;
        if ((k & KEY_ENTER) != 0u)
            endDstEdit(true);
        else if ((k & KEY_ESC) != 0u)
            endDstEdit(false);
        else if (!kbdSpaceOnHash() && ((k & KEY_HASH) != 0u)) {
            /* # clears the field (unified across the text editors); ENTER then
             * commits an empty destination as @ALL. */
            entry_->input.clear();
            renderDstEdit();
            announceDstChar();
        } else if ((k & KBD_CHAR_MASK) != 0u) {
            /* Numeric-keypad multi-tap (# handled above); '*' backspaces. */
            const uint8_t ki =
                static_cast<uint8_t>(__builtin_ctz(k & KBD_CHAR_MASK));
            if (ki == 10u)
                entry_->input.backspace();
            else
                entry_->input.tapKey(ki, getTick());
            renderDstEdit();
            announceDstChar();
        } else if ((k & KEY_UP) != 0u)
            dstCycle(+1);
        else if ((k & KEY_DOWN) != 0u)
            dstCycle(-1);
        else if ((k & KEY_LEFT) != 0u)
            dstMove(-1);
        else if ((k & KEY_RIGHT) != 0u)
            dstMove(+1);
    }
    return NavIntent::none();
}

void VfoView::announceFreq()
{
    if (state.settings.vpLevel < vpLow)
        return;
    vp_flush();
    vp_announceFrequencies(state.channel.rx_frequency,
                           state.channel.tx_frequency,
                           vp_getVoiceLevelQueueFlags());
    vp_play();
}

void VfoView::announceVfoState()
{
    /* In memory mode speak the full channel summary; when tuning speak the
     * frequency pair and mode. Mirrors the classic VFO/MEM announcements. */
    if (state.settings.vpLevel < vpLow)
        return;

    const enum vpQueueFlags flags = vp_getVoiceLevelQueueFlags();
    vp_flush();
    if (state.tuner_mode != VFO) {
        vp_announceChannelSummary(
            &state.channel, (uint16_t)(state.channel_index + 1), 0, vpAllInfo);
    } else {
        vp_announceFrequencies(state.channel.rx_frequency,
                               state.channel.tx_frequency, flags);
        vp_announceRadioMode(state.channel.mode, flags);
    }
    vp_play();
}

void VfoView::announce()
{
    announceVfoState();
}

} // namespace ortxui
