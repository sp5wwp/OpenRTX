/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/TunerView.hpp"
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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace ortxui
{

constexpr uint8_t TunerView::TUNER_COUNT;
constexpr uint8_t TunerView::LIVE_TUNERS;
constexpr uint32_t TunerView::TUNER_SELECT_KEY;

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
            v += static_cast<freq_t>(d) * slotCoeff(o, FREQ_SLOTS);
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

TunerView::~TunerView()
{
    if (entry != nullptr)
        closeEntry();
}

void TunerView::build()
{
    /* No title: the home screen's top bar shows the clock. */
    TopBar &bar = useTopBar("");
    const Rect screen = screenArea();
    const uint16_t barH = static_cast<uint16_t>(bar.layout().basis);
    bar.setArea({ screen.x, screen.y, screen.w, barH });
    tree.addChild(&bar);
    bar.onLayout();

    /* Several tuners, or one on the large screen, are cards. */
    Tuner::Style style = Tuner::Style::Card;
    if ((TUNER_COUNT == 1) && (sizeClass() < SizeClass::Large))
        style = (sizeClass() >= SizeClass::Regular) ? Tuner::Style::Regular :
                                                      Tuner::Style::Compact;

    /* The tuners share the rest of the screen, stacked under the bar. */
    const int32_t top = screen.y + barH;
    const int32_t span = screen.h - barH;
    for (uint8_t i = 0; i < TUNER_COUNT; i++) {
        const int32_t y0 = top + span * i / TUNER_COUNT;
        const int32_t y1 = top + span * (i + 1) / TUNER_COUNT;
        Tuner &t = tuners[i];
        t.setup(style, static_cast<uint8_t>(i + 1));
        t.setArea({ screen.x, static_cast<int16_t>(y0), screen.w,
                    static_cast<uint16_t>(y1 - y0) });
        tree.addChild(&t);
        t.onLayout();
    }
}

void TunerView::save() const
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

void TunerView::restore()
{
    EntryState s;
    if (!uiMemory.inlineEdit.take(id(), s) || !openEntry(s.destination))
        return;

    Entry &n = *entry;
    static_cast<EntryState &>(n) = s;
    if (n.destination) {
        bindDst(n.dstCursor);
        renderDstEdit();
        return;
    }

    bindField(n.txField, n.txField ? n.txSlot : n.rxSlot);
    refreshInput();
}

bool TunerView::openEntry(bool destination)
{
    void *mem = malloc(sizeof(Entry));
    if (mem == nullptr)
        return false;

    entry = new (mem) Entry();
    entry->destination = destination;
    return true;
}

void TunerView::closeEntry()
{
    entry->~Entry();
    free(entry);
    entry = nullptr;
}

/**
 * Compose the M17 destination label, "@" and the destination or "ALL".
 *
 * @param out: output buffer.
 * @param sz: size of out.
 */
static void m17DstLabel(char *out, uint16_t sz)
{
    const char *dst = state.settings.m17_dest;
    if (dst[0] == '\0')
        sniprintf(out, sz, "@%s", STR(broadcast).str());
    else
        sniprintf(out, sz, "@%s", dst);
}

bool TunerView::selectedLive()
{
    return uiMemory.selectedTuner < LIVE_TUNERS;
}

void TunerView::describe(uint8_t tuner, const state_t &s, uint8_t opStatus,
                         bool squelchOpen, TunerState &t)
{
    t.selected = (tuner == uiMemory.selectedTuner);

    /* A tuner with no receiver behind it never receives or transmits. */
    if (tuner >= LIVE_TUNERS) {
        t.placeholder = true;
        return;
    }

    const channel_t &ch = s.channel;
    const bool txing = (opStatus == TX);

    t.rxFreq = ch.rx_frequency;
    t.txFreq = ch.tx_frequency;
    t.txPower = ch.power;
    t.rssi = s.rssi;
    t.squelch = s.settings.sqlLevel;
    t.opMode = ch.mode;
    t.transmitting = txing;
    t.receiving = (opStatus == RX) && squelchOpen;

    switch (ch.mode) {
        case OPMODE_FM:
            t.mode = bandwidthLabel(ch.bandwidth).str();
            break;
        case OPMODE_DMR:
            t.mode = STR(dmr).str();
            break;
        case OPMODE_M17:
            t.mode = SYMBOL_M17;
            break;
        default:
            t.mode = "--";
            break;
    }

    /* The CAN shows only while it filters something: on TX or with the CAN
     * RX check on. The tone shows only when the current direction uses it. */
    t.detailTag = "";
    t.detail[0] = '\0';
    if (ch.mode == OPMODE_M17) {
        if (txing || state.settings.m17_can_rx) {
            t.detailTag = STR(CAN).str();
            sniprintf(t.detail, sizeof(t.detail), "%u",
                      static_cast<unsigned>(state.settings.m17_can));
        }
    } else if (ch.mode == OPMODE_FM) {
        const bool showTx = txing && (ch.fm.txToneEn != 0u);
        const bool showRx = !txing && (ch.fm.rxToneEn != 0u);
        if (showTx || showRx) {
            t.detailTag = STR(CTCSS).str();
            formatTone(showTx ? ch.fm.txTone : ch.fm.rxTone, t.detail,
                       sizeof(t.detail));
        }
    }

    /* The channel line: "VFO" or the memory index, and the channel name or
     * the M17 destination. An FM VFO has no name. */
    if (s.tuner_mode == VFO) {
        sniprintf(t.index, sizeof(t.index), "%s", STR(VFO).str());
        if (ch.mode == OPMODE_M17)
            m17DstLabel(t.name, sizeof(t.name));
        else
            t.name[0] = '\0';
    } else {
        sniprintf(t.index, sizeof(t.index), "%03u", s.channel_index + 1);
        sniprintf(t.name, sizeof(t.name), "%s",
                  (ch.name[0] != '\0') ? ch.name : "---");
    }

    /* While an M17 stream is received, its caller and destination take the
     * name's place. */
    if ((ch.mode == OPMODE_M17) && t.receiving) {
        const rtxStatus_t *rx = rtx_getStatus();
        if (rx->lsfOk)
            sniprintf(t.name, sizeof(t.name), "%.9s > %.9s", rx->M17_src,
                      rx->M17_dst);
    }
}

void TunerView::syncFromState(const state_t &s)
{
    View::syncFromState(s); /* refresh the shared top bar */

    /* Clear the out-of-band flag once it has been shown long enough. */
    if ((entry != nullptr) && (entry->errorAt != 0)
        && ((getTick() - entry->errorAt) >= INPUT_ERROR_MS)) {
        entry->errorAt = 0;
        refreshInput();
    }

    /* state.rtxStatus is the mode requested; rtx has the live one. */
    const uint8_t status = rtx_getStatus()->opStatus;
    const bool squelchOpen = rtx_rxSquelchOpen();
    for (uint8_t i = 0; i < TUNER_COUNT; i++) {
        TunerState t;
        describe(i, s, status, squelchOpen, t);
        tuners[i].update(t);
    }
}

void TunerView::selectNext()
{
    uiMemory.selectedTuner =
        static_cast<uint8_t>((uiMemory.selectedTuner + 1) % TUNER_COUNT);
    announce();
}

NavIntent TunerView::onEvent(const Event &e)
{
    if (entry != nullptr)
        return entry->destination ? onDstEditEvent(e) : onInputEvent(e);

    const bool key = (e.kind == EvKind::Key);
    if ((TUNER_COUNT > 1) && key && ((e.keys & TUNER_SELECT_KEY) != 0u)) {
        selectNext();
        return NavIntent::none();
    }

    /* A placeholder has nothing to tune: only the menu opens. */
    if (!selectedLive()) {
        if (key && ((e.keys & KEY_ENTER) != 0u))
            return NavIntent::push(ViewId::MainMenu);
        return NavIntent::none();
    }

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
                beginInput(static_cast<uint8_t>(d));
        }
        return NavIntent::none();
    }

    tree.dispatch(e);
    return NavIntent::none();
}

NavIntent TunerView::onInputEvent(const Event &e)
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

void TunerView::step(int dir)
{
    if (state.tuner_mode == VFO)
        stepFreq(dir);
    else
        stepChannel(dir);
}

void TunerView::stepFreq(int dir)
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

void TunerView::stepChannel(int dir)
{
    const int32_t index = state.channel_index + dir;
    if (loadChannel(static_cast<int16_t>(index))) {
        requestSyncRtx();
        if (state.settings.vpLevel >= vpLow)
            vp_announceChannelName(
                &state.channel, static_cast<uint16_t>(state.channel_index + 1),
                vp_getVoiceLevelQueueFlags());
    }
}

void TunerView::toggleVfoMem()
{
    if (state.tuner_mode == VFO) {
        /* Keep the VFO to come back to. */
        state.vfo_channel = state.channel;
        if (loadChannel(static_cast<int16_t>(state.channel_index))) {
            state.tuner_mode = CH;
            requestSyncRtx();
            if (state.settings.vpLevel >= vpLow)
                vp_announceChannelName(
                    &state.channel,
                    static_cast<uint16_t>(state.channel_index + 1),
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

bool TunerView::loadChannel(int16_t index)
{
    const int16_t selected = index;
    int16_t readIndex = index;

    if (state.bank_enabled) {
        bankHdr_t bank = {};
        cps_readBankHeader(&bank, state.bank);
        if ((index < 0) || (index >= static_cast<int16_t>(bank.ch_count)))
            return false;
        readIndex = static_cast<int16_t>(
            cps_readBankData(state.bank, static_cast<uint16_t>(index)));
    } else if (index < 0) {
        return false;
    }

    channel_t ch;
    if (cps_readChannel(&ch, static_cast<uint16_t>(readIndex)) != 0)
        return false;

    state.channel = ch;
    state.channel_index = static_cast<uint16_t>(selected);
    return true;
}

bool TunerView::openInput()
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

void TunerView::beginInput(uint8_t firstDigit)
{
    if (!openInput())
        return;

    entry->rxPristine = false;
    entry->input.clearSlots();
    entry->input.putDigit(firstDigit);
    announceDigit(firstDigit);
    refreshInput();
}

void TunerView::beginInputTweak()
{
    /* Left pre-filled: '*' backspaces from the end, a digit retypes it. */
    if (openInput())
        refreshInput();
}

void TunerView::switchField()
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

void TunerView::deriveTx()
{
    Entry &n = *entry;
    const freq_t rx = freqFromSlots(n.rx);
    /* Below 0 Hz is rejected as out of band at commit. */
    const int64_t tx = std::max<int64_t>(static_cast<int64_t>(rx) + n.shift, 0);
    slotsFromFreq(n.tx, static_cast<freq_t>(tx));
    n.txSlot = FREQ_SLOTS;
}

void TunerView::bindField(bool tx, uint8_t slot)
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

void TunerView::confirmInput()
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

void TunerView::exitInput()
{
    closeEntry();
    selectedTuner().endEntry();
}

void TunerView::refreshInput()
{
    const Entry &n = *entry;
    char main[16];
    char sub[6];
    composeEntry(n.txField ? n.tx : n.rx, main, sub);
    selectedTuner().showFreqEntry(main, sub, n.txField, n.errorAt != 0);
}

void TunerView::clearFmTone()
{
    if (state.tone_enabled) {
        state.tone_enabled = false;
        requestSyncRtx();
    }
}

void TunerView::renderDstEdit()
{
    /* The '@' stays pinned while the rest scrolls to keep the cursor in view,
     * so a full-length destination keeps the channel line's font. */
    const fontSize_t font = selectedTuner().nameFont();
    const uint16_t atW = gfx_getTextWidth(font, "@");
    const uint16_t boxW = selectedTuner().nameWidth();
    const uint16_t bodyW = (boxW > atW) ? static_cast<uint16_t>(boxW - atW) : 0;

    char body[36];
    entry->input.formatWindow(body, sizeof(body), bodyW, font);
    char text[sizeof(TunerState::name)];
    sniprintf(text, sizeof(text), "@%s", body);
    selectedTuner().showDestEntry(text);
}

void TunerView::bindDst(uint16_t cursor)
{
    Entry &n = *entry;
    n.input.configure(n.dst, sizeof(n.dst), CHARSET_CALLSIGN,
                      TextInput::Mode::SingleLine, FONT_SIZE_8PT);
    n.input.setMultiTap(MTAP_CALLSIGN);
    n.input.resume(cursor, 0);
}

void TunerView::announceDigit(uint8_t d)
{
    if (state.settings.vpLevel >= vpLow)
        vp_announceInputChar(static_cast<char>('0' + d));
}

void TunerView::announceDstChar()
{
    if (state.settings.vpLevel >= vpLow)
        vp_announceInputChar(entry->input.cursorChar());
}

void TunerView::beginDstEdit()
{
    if (!openEntry(true))
        return;

    Entry &n = *entry;
    strncpy(n.dst, state.settings.m17_dest, sizeof(n.dst) - 1);
    n.dst[sizeof(n.dst) - 1] = '\0';
    bindDst(0);
    renderDstEdit();
    announceDstChar();
}

void TunerView::endDstEdit(bool commit)
{
    if (commit) {
        strncpy(state.settings.m17_dest, entry->dst,
                sizeof(state.settings.m17_dest) - 1);
        state.settings.m17_dest[sizeof(state.settings.m17_dest) - 1] = '\0';
        requestSyncRtx(); /* the destination goes into the M17 LSF */
    }

    closeEntry();
    selectedTuner().endEntry();
}

NavIntent TunerView::onDstEditEvent(const Event &e)
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

void TunerView::announceFreq()
{
    if (state.settings.vpLevel < vpLow)
        return;
    vp_flush();
    vp_announceFrequencies(state.channel.rx_frequency,
                           state.channel.tx_frequency,
                           vp_getVoiceLevelQueueFlags());
    vp_play();
}

void TunerView::announce()
{
    if (state.settings.vpLevel < vpLow)
        return;

    /* A placeholder is its number and "none". */
    if (!selectedLive()) {
        vp_flush();
        vp_queueInteger(uiMemory.selectedTuner + 1);
        STR(None).announce(vpqDefault);
        vp_play();
        return;
    }

    const enum vpQueueFlags flags = vp_getVoiceLevelQueueFlags();
    vp_flush();
    if (state.tuner_mode != VFO) {
        vp_announceChannelSummary(
            &state.channel, static_cast<uint16_t>(state.channel_index + 1), 0,
            vpAllInfo);
    } else {
        vp_announceFrequencies(state.channel.rx_frequency,
                               state.channel.tx_frequency, flags);
        vp_announceRadioMode(state.channel.mode, flags);
    }
    vp_play();
}

} // namespace ortxui
