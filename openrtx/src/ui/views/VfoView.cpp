/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/VfoView.hpp"
#include "style/Symbols.hpp"
#include "widgets/Charsets.hpp"
#include "base/Event.hpp"
#include "base/UiFormat.hpp"
#include "views/UiMemory.hpp"
#include "base/Layout.hpp"
#include "interfaces/delays.h"
#include "core/utils.h"
#include "core/cps.h"
#include "rtx/rtx.h"
#include "interfaces/keyboard.h"
#include "interfaces/cps_io.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace ortxui
{

/**
 * Slot mask of a frequency field, 100 MHz to 100 Hz, without separators:
 * FreqHero lays out the MHz and kHz groups.
 */
static constexpr char FREQ_MASK[] = "9999999";

/** Slots of a frequency field. */
static constexpr uint16_t FREQ_SLOTS = sizeof(FREQ_MASK) - 1;

/** What an empty slot shows. */
static constexpr char PLACEHOLDER = '-';

/** How long an out-of-band entry stays flagged. */
static constexpr long long INPUT_ERROR_MS = 1500;

/** Thickness of the meter's bar. */
static constexpr uint8_t METER_BAR_H = 7;

/** Range of the TX meter's dB scale, which is full at POWER_MAX_MW. */
static constexpr float POWER_SPAN_DB = 10.0f;

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
 * Get the meter fill fraction for a TX power on the dB scale.
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
    return clampFrac(1.0f + db / POWER_SPAN_DB);
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
    /* S1-S9 rise 6 dB a point (-121 to -73), S9-S11 10 dB (-73 to -53). */
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
 * each earlier slot ten times larger.
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
    return (digits[o] != PLACEHOLDER) ? (digits[o] - '0') : -1;
}

/**
 * Read a slot field back as a frequency; empty slots count as 0.
 *
 * @param digits: field buffer.
 * @return the frequency in Hz.
 */
static freq_t freqFromSlots(const char *digits)
{
    freq_t v = 0;
    for (uint16_t o = 0; o < FREQ_SLOTS; o++) {
        const int d = slotDigit(digits, o);
        if (d >= 0)
            v += (freq_t)d * slotCoeff(o, FREQ_SLOTS);
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
    for (uint16_t o = 0; o < FREQ_SLOTS; o++)
        digits[o] =
            static_cast<char>('0' + (hz / slotCoeff(o, FREQ_SLOTS)) % 10);
}

/**
 * Compose the FreqHero texts of a frequency field: "MMM.KKK", and the 100 Hz
 * digit with a fixed 10 Hz zero. Empty slots show PLACEHOLDER.
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
    sub[0] = (d6 >= 0) ? static_cast<char>('0' + d6) : PLACEHOLDER;
    sub[1] = (d6 >= 0) ? '0' : PLACEHOLDER;
    sub[2] = '\0';
}

VfoView::~VfoView()
{
    if (entry != nullptr)
        closeEntry();
}

void VfoView::build()
{
    const bool regular = (sizeClass() == SizeClass::Regular);

    root.setArea(screenArea());
    root.setAxis(Axis::Column);

    /* No title: the home screen's top bar shows the clock. */
    TopBar &bar = useTopBar("");

    /* Taller than the text on the roomy screen; just clearing it on the
     * compact one, so the channel line and the meter fit in 64 px. */
    hero.setBasis(regular ? 40 : 26);

    /* The channel line in one font; rows a full line high, so descenders and
     * the '@' of a destination are not clipped. */
    const fontSize_t chanFont = regular ? FONT_SIZE_8PT : FONT_SIZE_6PT;
    const int16_t rowH = gfx_getFontLineHeight(chanFont);

    /* "VFO" or the 1-based memory index, wide enough for "VFO". */
    chanIdx.setFont(chanFont);
    chanIdx.setAlign(TEXT_ALIGN_LEFT);
    chanIdx.setColor(Sem::OnSurfaceMuted);
    chanIdx.setBasis(regular ? 34 : 26);

    /* Channel name or M17 destination; on the roomy screen on a line of its
     * own, so a long destination shows in full. */
    chanName.setFont(chanFont);
    chanName.setAlign(TEXT_ALIGN_LEFT);
    chanName.setColor(Sem::Primary);
    chanName.setGrow(1);
    /* A name wider than the line ellipsizes rather than wrapping. */
    chanName.setOverflow(Label::Overflow::Ellipsize);

    /* A small CAN or CTCSS tag, then the value. */
    chanDetail.setRun(0, FONT_SIZE_5PT, Sem::OnSurfaceMuted);
    chanDetail.setRun(1, chanFont, Sem::OnSurfaceMuted);
    chanDetail.setGap(3);
    /* Wide enough for the longest tagged value, "CTCSS 254.1". */
    chanDetail.setBasis(regular ? 70 : 48);

    /* The meter sits at the bottom under a growing spacer, tall enough for
     * the label and readout over the S-scale numbers. */
    meter.setBasis(regular ? 19 : 15);
    meter.setBarHeight(METER_BAR_H);
    meter.setColors(Sem::SurfaceHigh, Sem::RxSuccess);
    meter.setTextColor(Sem::RxSuccess);
    meter.setShowScale(true);
    meter.setLabel(STR(RX));
    spacer.setGrow(1);

    root.addChild(&bar);
    root.addChild(&hero);

    if (regular) {
        /* Roomy layout: the name on its own line, then the index (left) and
         * the CAN / tone detail (right). */
        nameRow.setAxis(Axis::Row);
        nameRow.setAlign(Align::Center);
        nameRow.setJustify(Justify::Start);
        nameRow.setPadding(4, 0);
        nameRow.setBasis(rowH);
        nameRow.addChild(&chanName);

        metaRow.setAxis(Axis::Row);
        metaRow.setAlign(Align::Center);
        metaRow.setJustify(Justify::SpaceBetween);
        metaRow.setPadding(4, 0);
        metaRow.setBasis(rowH);
        metaRow.addChild(&chanIdx);
        metaRow.addChild(&chanDetail);

        root.addChild(&nameRow);
        root.addChild(&metaRow);
    } else {
        /* Compact layout: no room for a second line, so the index, name and
         * detail share one row and the name ellipsizes. */
        nameRow.setAxis(Axis::Row);
        nameRow.setAlign(Align::Center);
        nameRow.setJustify(Justify::Start);
        nameRow.setPadding(4, 0);
        nameRow.setGap(6);
        nameRow.setBasis(rowH);
        nameRow.addChild(&chanIdx);
        nameRow.addChild(&chanName);
        nameRow.addChild(&chanDetail);

        root.addChild(&nameRow);
    }

    root.addChild(&spacer);
    root.addChild(&meter);
    tree.addChild(&root);
    root.onLayout();

    invalidateReadout();
}

void VfoView::save() const
{
    if (entry == nullptr)
        return;

    /* Take the live cursor from the TextInput. */
    EntryState s = *entry;
    const uint8_t slot = static_cast<uint8_t>(entry->input.cursorSlot());
    if (s.destination)
        s.dstCursor = static_cast<uint8_t>(entry->input.cursor());
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

    Entry &n = *entry;
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

    entry = new (mem) Entry();
    entry->destination = destination;
    return true;
}

void VfoView::closeEntry()
{
    entry->~Entry();
    free(entry);
    entry = nullptr;
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
            m1 = SYMBOL_M17;
            break;
        default:
            break;
    }

    /* The CAN shows only while it filters something: on TX or with the CAN
     * RX check on. The tone shows only when the current direction uses it. */
    const char *tag = "";
    char sub[sizeof(modeSub)] = "";
    if (ch.mode == OPMODE_M17) {
        if (txing || state.settings.m17_can_rx) {
            tag = STR(CAN).str();
            sniprintf(sub, sizeof(sub), "%u", (unsigned)state.settings.m17_can);
        }
    } else if (ch.mode == OPMODE_FM) {
        const bool showTx = txing && (ch.fm.txToneEn != 0u);
        const bool showRx = !txing && (ch.fm.rxToneEn != 0u);
        if (showTx || showRx) {
            tag = STR(CTCSS).str();
            formatTone(showTx ? ch.fm.txTone : ch.fm.rxTone, sub, sizeof(sub));
        }
    }

    /* The texts are literals or table entries, so the pointers kept stay
     * valid. */
    const bool same = (shownMode != nullptr) && (strcmp(m1, shownMode) == 0)
                   && (strcmp(tag, shownTag) == 0)
                   && (strcmp(sub, modeSub) == 0);
    if (same)
        return;
    shownMode = m1;
    shownTag = tag;
    memcpy(modeSub, sub, sizeof(modeSub));

    hero.setMode(m1);
    hero.invalidate();
    chanDetail.setText(0, tag);
    chanDetail.setText(1, modeSub);
    chanDetail.invalidate();
}

void VfoView::syncMeter(const state_t &s, uint8_t status)
{
    const uint8_t sql = s.settings.sqlLevel;
    const uint8_t mode = s.channel.mode;
    const bool restyle = (status != lastStatus) || (sql != lastSql)
                      || (mode != lastMode);
    if (!restyle && (s.rssi == lastRssi))
        return;

    lastStatus = status;
    lastRssi = s.rssi;
    lastSql = sql;
    lastMode = mode;

    char readout[sizeof(readoutBuf)];
    if (status == TX) {
        const uint32_t mw = s.channel.power;

        if (restyle) {
            meter.setLabel(STR(TX));
            meter.setColors(Sem::SurfaceHigh, Sem::TxDanger);
            meter.setTextColor(Sem::TxDanger);
            meter.setShowScale(false);
            meter.setMarker(-1.0f); /* squelch gates RX only */
        }
        meter.setValue(powerToFrac(mw));

        formatPower(mw, readout, sizeof(readout));
    } else {
        const uint8_t level = rssiToSlevel(s.rssi);

        if (restyle) {
            meter.setLabel(STR(RX));
            meter.setColors(Sem::SurfaceHigh, Sem::RxSuccess);
            meter.setTextColor(Sem::RxSuccess);
            meter.setShowScale(true);

            /* The mark sits where the signal has to reach; only FM gates on
             * RSSI. */
            if (mode == OPMODE_FM)
                meter.setMarker(
                    rssiToFrac(static_cast<float>(squelchToRssi(sql))));
            else
                meter.setMarker(-1.0f);
        }
        meter.setValue(rssiToFrac(static_cast<float>(s.rssi)));

        if (level > 9u)
            sniprintf(readout, sizeof(readout), "+%u",
                      (unsigned)((level - 9u) * 10u));
        else
            sniprintf(readout, sizeof(readout), "S%u", (unsigned)level);
    }

    /* Repaint on a change of the readout or of the fill in pixels, not on
     * every dBm. */
    const bool text = (strcmp(readout, readoutBuf) != 0);
    if (text) {
        memcpy(readoutBuf, readout, sizeof(readoutBuf));
        meter.setReadout(readoutBuf);
    }
    const uint16_t fill = meter.fillWidth();
    if (restyle || text || (fill != lastFill))
        meter.invalidate();
    lastFill = fill;
}

void VfoView::syncFromState(const state_t &s)
{
    View::syncFromState(s); /* refresh the shared top bar */

    /* state.rtxStatus is the mode requested; rtx has the live one. */
    const uint8_t status = rtx_getStatus()->opStatus;
    const bool txing = (status == TX);

    /* An open frequency entry owns the hero and the channel line. */
    const bool input = (entry != nullptr) && !entry->destination;
    if (!input) {
        const channel_t &ch = s.channel;

        /* Show the frequency the radio is on: TX while transmitting. */
        const freq_t shown = (txing && (ch.tx_frequency != ch.rx_frequency)) ?
                                 ch.tx_frequency :
                                 ch.rx_frequency;
        if ((uint32_t)shown != lastFreq) {
            hero.setFreq((uint32_t)shown);
            hero.invalidate();
            lastFreq = (uint32_t)shown;
        }

        syncMode(ch, txing);

        /* An open destination edit owns the channel line. */
        if (entry == nullptr) {
            char idxText[sizeof(idxCache)];
            char nameText[sizeof(nameCache)];
            composeChanLine(s, idxText, nameText, sizeof(nameText));

            if (strncmp(idxText, idxCache, sizeof(idxCache)) != 0) {
                strncpy(idxCache, idxText, sizeof(idxCache) - 1);
                idxCache[sizeof(idxCache) - 1] = '\0';
                chanIdx.setText(idxCache);
                chanIdx.invalidate();
            }
            if (strncmp(nameText, nameCache, sizeof(nameCache)) != 0) {
                strncpy(nameCache, nameText, sizeof(nameCache) - 1);
                nameCache[sizeof(nameCache) - 1] = '\0';
                chanName.setText(nameCache);
                chanName.invalidate();
            }
        }
    } else if (entry->errorAt != 0) {
        /* Clear the out-of-band flag once it has been shown long enough. */
        if ((getTick() - entry->errorAt) >= INPUT_ERROR_MS) {
            entry->errorAt = 0;
            refreshInput();
        }
    }

    syncMeter(s, status);
}

NavIntent VfoView::onEvent(const Event &e)
{
    if (entry != nullptr)
        return entry->destination ? onDstEditEvent(e) : onInputEvent(e);

    /* The knob tunes in VFO mode and steps channels in memory mode. */
    if (e.kind == EvKind::Encoder) {
        clearFmTone();
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
            /* '*' edits the current frequency in place. */
            if (state.tuner_mode == VFO)
                beginInputTweak();
        } else if (state.tuner_mode == VFO) {
            /* A digit retypes the frequency. */
            const int d = digitFromKeys(k);
            if (d >= 0)
                beginInput((uint8_t)d);
        }
        return NavIntent::none();
    }

    tree.dispatch(e);
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
        exitInput();
        return NavIntent::none();
    }
    if ((k & (KEY_UP | KEY_DOWN)) != 0u) {
        switchField();
        return NavIntent::none();
    }

    /* Only a key that edits the field may end its pre-filled state. */
    const int digit = digitFromKeys(k);
    const bool mutating = (digit >= 0) || ((k & KEY_STAR) != 0u)
                       || ((k & (KEY_LEFT | KEY_RIGHT)) != 0u);
    if (!mutating)
        return NavIntent::none();

    Entry &n = *entry;
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

    /* An underflow wraps to a huge value, out of band like any other. */
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
        /* Keep the VFO to come back to. */
        state.vfo_channel = state.channel;
        if (loadChannel((int16_t)state.channel_index)) {
            state.tuner_mode = CH;
            requestSyncRtx();
            if (state.settings.vpLevel >= vpLow)
                vp_announceChannelName(&state.channel,
                                       (uint16_t)(state.channel_index + 1),
                                       vp_getVoiceLevelQueueFlags());
        }
        /* Without a channel to load, stay in VFO mode. */
    } else {
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

    Entry &n = *entry;
    const channel_t &ch = state.channel;
    n.shift = static_cast<int32_t>(static_cast<int64_t>(ch.tx_frequency)
                                   - static_cast<int64_t>(ch.rx_frequency));

    /* RX is pre-filled, cursor at the end; TX is derived when first shown. */
    memset(n.tx, PLACEHOLDER, FREQ_SLOTS);
    n.tx[FREQ_SLOTS] = '\0';
    slotsFromFreq(n.rx, ch.rx_frequency);
    n.rx[FREQ_SLOTS] = '\0';
    bindField(false, FREQ_SLOTS);
    return true;
}

void VfoView::beginInput(uint8_t firstDigit)
{
    if (!openInput())
        return;

    entry->rxPristine = false;
    entry->input.clearSlots();
    entry->input.putDigit(firstDigit);
    announceDigit(firstDigit);
    refreshInput();
}

void VfoView::beginInputTweak()
{
    /* Left pre-filled: '*' backspaces from the end, a digit retypes it. */
    if (openInput())
        refreshInput();
}

void VfoView::switchField()
{
    Entry &n = *entry;
    if (!n.txField && n.txPristine)
        deriveTx();

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
    Entry &n = *entry;
    const freq_t rx = freqFromSlots(n.rx);
    /* Below 0 Hz is rejected as out of band at commit. */
    const int64_t tx = std::max<int64_t>(static_cast<int64_t>(rx) + n.shift, 0);
    slotsFromFreq(n.tx, static_cast<freq_t>(tx));
    n.txSlot = FREQ_SLOTS;
}

void VfoView::bindField(bool tx, uint8_t slot)
{
    /* Binding lays the mask over the buffer: put the digits back after. */
    Entry &n = *entry;
    char *buf = tx ? n.tx : n.rx;
    char digits[FREQ_DIGITS + 1];
    memcpy(digits, buf, sizeof(digits));
    n.input.configureSlots(buf, FREQ_DIGITS + 1, FREQ_MASK, PLACEHOLDER,
                           FONT_SIZE_8PT);
    memcpy(buf, digits, sizeof(digits));
    n.input.setCursorSlot(slot);
}

void VfoView::confirmInput()
{
    /* ENTER on RX shows TX first; ENTER on TX applies both. */
    Entry &n = *entry;
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
        /* Keep the entry and flag it until the next edit or INPUT_ERROR_MS. */
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
    lastFreq = UINT32_MAX;
    shownMode = nullptr;
    idxCache[0] = '\1';
    nameCache[0] = '\1';
    tree.markAllDirty();
}

void VfoView::refreshInput()
{
    const Entry &n = *entry;
    char main[16];
    char sub[6];
    composeEntry(n.txField ? n.tx : n.rx, main, sub);

    const bool err = (n.errorAt != 0);
    hero.setEntry(main, sub);
    hero.setMode((n.txField ? STR(TX) : STR(RX)).str());
    hero.setTextColor(err ? Sem::Mark : Sem::Accent);
    hero.invalidate();

    chanIdx.setText("");
    chanName.setText(err ? STR(outOfBand) :
                           (n.txField ? STR(enterTx) : STR(enterRx)));
    chanDetail.setText(0, "");
    chanDetail.setText(1, "");
    chanIdx.invalidate();
    chanName.invalidate();
    chanDetail.invalidate();
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
        sniprintf(out, sz, "@%s", STR(broadcast).str());
    else
        sniprintf(out, sz, "@%s", dst);
}

void VfoView::composeChanLine(const state_t &s, char *idxOut, char *nameOut,
                              uint16_t nameSz)
{
    const channel_t &ch = s.channel;

    if (s.tuner_mode == VFO) {
        /* An FM VFO has no name; M17 shows the destination there. */
        sniprintf(idxOut, sizeof(idxCache), "%s", STR(VFO).str());
        if (ch.mode == OPMODE_M17)
            m17DstLabel(nameOut, nameSz);
        else
            nameOut[0] = '\0';
    } else {
        sniprintf(idxOut, sizeof(idxCache), "%03u", s.channel_index + 1);
        sniprintf(nameOut, nameSz, "%s",
                  (ch.name[0] != '\0') ? ch.name : "---");
    }
}

void VfoView::renderDstEdit()
{
    /* The '@' stays pinned while the rest scrolls to keep the cursor in view,
     * so a full-length destination keeps the channel line's font. */
    const fontSize_t font = chanName.font();
    const uint16_t atW = gfx_getTextWidth(font, "@");
    const uint16_t boxW = chanName.area().w;
    const uint16_t bodyW = (boxW > atW) ? static_cast<uint16_t>(boxW - atW) : 0;

    char body[36];
    entry->input.formatWindow(body, sizeof(body), bodyW, font);
    sniprintf(nameCache, sizeof(nameCache), "@%s", body);
    idxCache[0] = '\0';
    chanIdx.setText(idxCache);
    chanName.setText(nameCache);
    chanIdx.invalidate();
    chanName.invalidate();
}

void VfoView::showDstEdit(bool editing)
{
    if (sizeClass() == SizeClass::Regular)
        return;

    chanIdx.setFlag(FlagHidden, editing);
    nameRow.onLayout();
}

void VfoView::bindDst(uint16_t cursor)
{
    Entry &n = *entry;
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
        vp_announceInputChar(entry->input.cursorChar());
}

void VfoView::beginDstEdit()
{
    if (!openEntry(true))
        return;

    Entry &n = *entry;
    strncpy(n.dst, state.settings.m17_dest, sizeof(n.dst) - 1);
    n.dst[sizeof(n.dst) - 1] = '\0';
    bindDst(0);
    showDstEdit(true);
    renderDstEdit();
    tree.markAllDirty();
    announceDstChar();
}

void VfoView::endDstEdit(bool commit)
{
    if (commit) {
        strncpy(state.settings.m17_dest, entry->dst,
                sizeof(state.settings.m17_dest) - 1);
        state.settings.m17_dest[sizeof(state.settings.m17_dest) - 1] = '\0';
        requestSyncRtx(); /* the destination goes into the M17 LSF */
    }

    closeEntry();
    showDstEdit(false);
    invalidateReadout();
}

NavIntent VfoView::onDstEditEvent(const Event &e)
{
    /* An empty destination is stored as is, and shown as @ALL. */
    switch (entry->input.handleEvent(e, getTick())) {
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
