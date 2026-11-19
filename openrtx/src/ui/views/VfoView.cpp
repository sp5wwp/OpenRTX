/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/VfoView.hpp"
#include "style/Symbols.hpp"
#include "style/Charsets.hpp"
#include "core/Event.hpp"
#include "core/UiFormat.hpp"
#include "core/UiMemory.hpp"
#include "core/Layout.hpp"
#include "interfaces/delays.h"
#include "core/utils.h"
#include "core/cps.h"
#include "rtx/rtx.h"
#include "interfaces/keyboard.h"
#include "interfaces/cps_io.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"

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

/* The TX meter is a dB scale up to the highest power the macro menu sets,
 * emptying kPowerSpanDb below it. */
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
        * std::log10(static_cast<float>(mw) / static_cast<float>(POWER_MAX_MW));
    return clampFrac(1.0f + db / kPowerSpanDb);
}

/**
 * Get the meter fill fraction for an RSSI: rssiToSlevel() made continuous and
 * scaled to S11, so the fill and the squelch mark do not snap to S-points.
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
    const uint32_t digits = keys & KBD_NUM_MASK;
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
 * 2-digit sub (the 100 Hz slot + a fixed 10 Hz zero), with kPlaceholder for
 * un-entered slots so the operator sees how many digits remain.
 *
 * @param digits: field buffer.
 * @param main: main text output, 8 bytes.
 * @param sub: sub text output, 3 bytes.
 */
static void composeEntry(const char *digits, char *main, char *sub)
{
    /* An empty slot holds the placeholder, which is what it shows. */
    main[0] = digits[0];
    main[1] = digits[1];
    main[2] = digits[2];
    main[3] = '.';
    main[4] = digits[3];
    main[5] = digits[4];
    main[6] = digits[5];
    main[7] = '\0';

    const int d6 = slotDigit(digits, 6);
    sub[0] = (d6 >= 0) ? static_cast<char>('0' + d6) : kPlaceholder;
    sub[1] = (d6 >= 0) ? '0' : kPlaceholder;
    sub[2] = '\0';
}

VfoView::~VfoView()
{
    if (entry_ != nullptr)
        closeEntry();
}

void VfoView::build()
{
    const bool regular = (sizeClass() == SizeClass::Regular);

    root_.setArea(screenArea());
    root_.setAxis(Axis::Column);

    /* Shared top bar with a blank title (VFO shows only the status cluster). */
    TopBar &bar = useTopBar("");

    /* Taller than the text on the roomy screen; just clearing it on the
     * compact one, so the channel line and the meter fit in 64 px. */
    hero_.setBasis(regular ? 40 : 26);

    /* The channel line in one font; rows a full line high, so descenders and
     * the '@' of a destination are not clipped. */
    const fontSize_t chanFont = regular ? FONT_SIZE_8PT : FONT_SIZE_6PT;
    const int16_t rowH = gfx_getFontLineHeight(chanFont);

    /* "VFO" or the 1-based memory index, wide enough for "VFO". */
    chanIdx_.setFont(chanFont);
    chanIdx_.setAlign(TEXT_ALIGN_LEFT);
    chanIdx_.setColor(Sem::OnSurfaceMuted);
    chanIdx_.setBasis(regular ? 34 : 26);

    /* Channel name or M17 destination; on the roomy screen on a line of its
     * own, so a long destination shows in full. */
    chanName_.setFont(chanFont);
    chanName_.setAlign(TEXT_ALIGN_LEFT);
    chanName_.setColor(Sem::Primary);
    chanName_.setGrow(1);
    /* A name wider than the line ellipsizes rather than wrapping. */
    chanName_.setOverflow(Label::Overflow::Ellipsize);

    /* A small "CAN" or "CTCSS" tag and the value, right-aligned. */
    chanDetail_.setRun(0, FONT_SIZE_5PT, Sem::OnSurfaceMuted); /* tag */
    chanDetail_.setRun(1, chanFont, Sem::OnSurfaceMuted);      /* value */
    chanDetail_.setGap(3);
    /* Wide enough for the longest tagged value, "CTCSS 254.1". */
    chanDetail_.setBasis(regular ? 70 : 48);

    /* The meter sits at the bottom under a growing spacer, tall enough for
     * the label and readout over the S-scale numbers. */
    meter_.setBasis(regular ? 19 : 15);
    meter_.setColor(Sem::RxSuccess);
    meter_.setLabel("RX");
    spacer_.setGrow(1);

    root_.addChild(&bar);
    root_.addChild(&hero_);

    if (regular) {
        /* Roomy layout: the name on its own line, then the index (left) and
         * the CAN / tone detail (right). */
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
         * detail share one row and the name ellipsizes. */
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

    invalidateReadout();
}

void VfoView::save() const
{
    if (entry_ == nullptr)
        return;

    /* Fold the live cursor into the state kept. */
    EntryState s = *entry_;
    const uint8_t slot = static_cast<uint8_t>(entry_->input.cursorSlot());
    if (s.destination)
        s.dstCursor = static_cast<uint8_t>(entry_->input.cursor());
    else if (s.txField)
        s.txSlot = slot;
    else
        s.rxSlot = slot;
    uiMemory.inlineEdit.keep(id(), s);
}

void VfoView::restore()
{
    EntryState s;
    if (!uiMemory.inlineEdit.take(id(), s) || !openEntry(s.destination))
        return;

    Entry &n = *entry_;
    static_cast<EntryState &>(n) = s;
    if (n.destination) {
        bindDst(n.dstCursor);
        showDstEdit(true);
        renderDstEdit();
        return;
    }

    bindField(n.txField, n.txField ? n.txSlot : n.rxSlot);
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

void VfoView::syncMode(const channel_t &ch, bool txing)
{
    const char *m1 = "--";
    switch (ch.mode) {
        case OPMODE_FM:
            m1 = bandwidthLabel(ch.bandwidth);
            break;
        case OPMODE_DMR:
            m1 = STR(dmr).str();
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
        if (txing || state.settings.m17_can_rx) {
            tag = STR(CAN).str();
            sniprintf(sub, sizeof(sub), "%u", (unsigned)state.settings.m17_can);
        }
    } else if (ch.mode == OPMODE_FM) {
        const bool showTx = txing && (ch.fm.txToneEn != 0u);
        const bool showRx = !txing && (ch.fm.rxToneEn != 0u);
        if (showTx || showRx) {
            tag = "CTCSS";
            formatTone(showTx ? ch.fm.txTone : ch.fm.rxTone, sub, sizeof(sub));
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

void VfoView::syncMeter(const state_t &s, uint8_t status)
{
    const uint8_t sql = s.settings.sqlLevel;
    const uint8_t mode = s.channel.mode;
    const bool restyle = (status != lastStatus_) || (sql != lastSql_)
                      || (mode != lastMode_);
    if (!restyle && (s.rssi == lastRssi_))
        return;

    lastStatus_ = status;
    lastRssi_ = s.rssi;
    lastSql_ = sql;
    lastMode_ = mode;

    char readout[sizeof(readoutBuf_)];
    if (status == TX) {
        const uint32_t mw = s.channel.power;

        if (restyle) {
            meter_.setLabel("TX");
            meter_.setColor(Sem::TxDanger);
            meter_.setShowScale(false);
            meter_.setMarker(-1.0f); /* squelch is an RX/FM concept */
        }
        /* Fill on a dB power scale relative to the radio's max output. */
        meter_.setValue(powerToFrac(mw));

        formatPower(mw, readout, sizeof(readout));
    } else {
        const uint8_t level = rssiToSlevel(s.rssi);

        if (restyle) {
            meter_.setLabel("RX");
            meter_.setColor(Sem::RxSuccess);
            meter_.setShowScale(true);

            /* Map the squelch threshold like the fill, so the mark sits
             * where the signal has to reach. Only FM gates on RSSI. */
            if (mode == OPMODE_FM)
                meter_.setMarker(
                    rssiToFrac(static_cast<float>(squelchToRssi(sql))));
            else
                meter_.setMarker(-1.0f);
        }
        meter_.setValue(rssiToFrac(static_cast<float>(s.rssi)));

        if (level > 9u)
            sniprintf(readout, sizeof(readout), "+%u",
                      (unsigned)((level - 9u) * 10u));
        else
            sniprintf(readout, sizeof(readout), "S%u", (unsigned)level);
    }

    /* Repaint only when what the meter draws changes, the readout or the
     * fill's width in pixels, not on every dBm of RSSI. */
    const bool text = (strcmp(readout, readoutBuf_) != 0);
    if (text) {
        memcpy(readoutBuf_, readout, sizeof(readoutBuf_));
        meter_.setReadout(readoutBuf_);
    }
    const uint16_t fill = meter_.fillWidth();
    if (restyle || text || (fill != lastFill_))
        meter_.invalidate();
    lastFill_ = fill;
}

void VfoView::syncFromState(const state_t &s)
{
    View::syncFromState(s); /* shared top bar */

    /* Live TX/RX comes from the rtx module; state.rtxStatus is only the
     * requested mode. */
    const uint8_t status = rtx_getStatus()->opStatus;
    const bool txing = (status == TX);

    /* While entering a frequency, the hero and channel line show the keypad
     * buffer (driven by refreshInput()); don't let live state overwrite them. */
    const bool input = (entry_ != nullptr) && !entry_->destination;
    if (!input) {
        const channel_t &ch = s.channel;

        /* While actually transmitting a channel with an RX/TX split (repeater
         * offset), show the TX frequency the radio is keyed up on; otherwise
         * (idle, or simplex) show the RX frequency. Gating on the *shown* value
         * repaints on both frequency changes and TX start/stop. */
        const freq_t shown = (txing && (ch.tx_frequency != ch.rx_frequency)) ?
                                 ch.tx_frequency :
                                 ch.rx_frequency;
        if ((uint32_t)shown != lastFreq_) {
            hero_.setFreq((uint32_t)shown);
            hero_.invalidate();
            lastFreq_ = (uint32_t)shown;
        }

        syncMode(ch, txing);

        /* Channel line (skipped while editing the destination, which drives it
         * directly): VFO mode shows "VFO" (or the M17 destination) with no
         * index; memory mode shows the 1-based index + channel name. Compose
         * the slot text and repaint only on a string change. */
        if (entry_ == nullptr) {
            char idxText[sizeof(idxCache_)];
            char nameText[sizeof(nameCache_)];
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

    syncMeter(s, status);
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

        /* The FM tone burst plays while # is held: any other event, its
         * release included, ends it. */
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
    n.input.handlePrefilledKey(k, n.txField ? n.txPristine : n.rxPristine,
                               getTick());

    n.errorAt = 0;
    if (digit >= 0)
        announceDigit(static_cast<uint8_t>(digit));
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
    n.shift = static_cast<int32_t>(static_cast<int64_t>(ch.tx_frequency)
                                   - static_cast<int64_t>(ch.rx_frequency));

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
    announceDigit(firstDigit);
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
    /* ENTER on RX advances to TX, derived from the offset, so the TX frequency
     * is seen before anything is applied; ENTER on TX commits. */
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
    invalidateReadout();
}

void VfoView::invalidateReadout()
{
    /* No shown text starts with \1, so the next sync repaints them all. */
    lastFreq_ = UINT32_MAX;
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
        sniprintf(idxOut, sizeof(idxCache_), "VFO");
        if (ch.mode == OPMODE_M17)
            m17DstLabel(nameOut, nameSz);
        else
            nameOut[0] = '\0';
    } else {
        sniprintf(idxOut, sizeof(idxCache_), "%03u", s.channel_index + 1);
        sniprintf(nameOut, nameSz, "%s",
                  (ch.name[0] != '\0') ? ch.name : "---");
    }
}

void VfoView::renderDstEdit()
{
    /* The '@' stays pinned while the rest scrolls to keep the cursor in view,
     * so a full-length destination keeps the channel line's font. */
    const fontSize_t font = chanName_.font();
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

void VfoView::showDstEdit(bool editing)
{
    if (sizeClass() == SizeClass::Regular)
        return;

    chanIdx_.setFlag(FLAG_HIDDEN, editing);
    nameRow_.onLayout();
}

void VfoView::bindDst(uint16_t cursor)
{
    Entry &n = *entry_;
    n.input.configure(n.dst, sizeof(n.dst), CHARSET_CALLSIGN,
                      TextInput::Mode::SingleLine, FONT_SIZE_8PT);
    n.input.setMultiTap(MTAP_CALLSIGN);
    n.input.resume(cursor, 0);
}

void VfoView::announceDigit(uint8_t d)
{
    if (state.settings.vpLevel >= vpLow)
        vp_announceInputChar(static_cast<char>('0' + d));
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
    bindDst(0);
    showDstEdit(true);
    renderDstEdit();
    screen_.markAllDirty();
    announceDstChar();
}

void VfoView::endDstEdit(bool commit)
{
    if (commit) {
        strncpy(state.settings.m17_dest, entry_->dst,
                sizeof(state.settings.m17_dest) - 1);
        state.settings.m17_dest[sizeof(state.settings.m17_dest) - 1] = '\0';
        requestSyncRtx(); /* the destination feeds the M17 LSF */
    }

    closeEntry();
    showDstEdit(false);
    invalidateReadout();
}

NavIntent VfoView::onDstEditEvent(const Event &e)
{
    /* An empty destination is stored as is, and shown as @ALL. */
    switch (entry_->input.handleEvent(e, getTick())) {
        case TextInput::KeyResult::Committed:
            endDstEdit(true);
            break;

        case TextInput::KeyResult::Cancelled:
            endDstEdit(false);
            break;

        case TextInput::KeyResult::Editing:
            renderDstEdit();
            announceDstChar();
            break;

        default:
            break;
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

void VfoView::announce()
{
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

} // namespace ortxui
