/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_VFOVIEW_HPP
#define ORTX_UI_VFOVIEW_HPP

#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/Label.hpp"
#include "widgets/TextRow.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/FreqHero.hpp"
#include "widgets/SignalMeter.hpp"
#include "widgets/TextInput.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The VFO home screen (mockups 5-6): a left-weighted instrument readout.
 *
 * A Flex column: shared top bar / frequency hero (big freq + sub-digits + mode
 * label) / the channel name or M17 destination as a full-width blue headline /
 * a meta strip (index left, M17 CAN or FM tone right) / open space / the signal
 * meter pinned to the bottom. The compact (64px) screen has no room for the
 * two-line identity block, so there the index, name and detail share one row
 * (the name ellipsizes). Widgets are direct members in static storage. build()
 * wires the tree and lays it out once; syncFromState() pulls the shown radio
 * fields from the state snapshot change-gated, and drives the meter from the
 * RX/TX status (green RX with an S-scale, orange TX as a solid power bar).
 *
 * As the navigation root, ENTER drills into the main menu (wired via setMenu()).
 * The screen is also interactive: UP/DOWN (or the knob) tune the frequency in
 * VFO mode or step through channels in memory mode; ESC toggles between VFO and
 * memory mode; a digit key opens an in-place frequency keypad entry (RX then TX
 * set). Radio-affecting edits raise requestSyncRtx(). Mirrors the classic VFO.
 */
class VfoView : public View
{
public:
    /** Constructor. */
    VfoView();

    /** Destructor. */
    ~VfoView();

    void build() override;

    /** Keep an open frequency or destination entry while destroyed. */
    void save(UiMemory &m) const override;

    /** Reopen a frequency or destination entry kept by save(). */
    void restore(UiMemory &m) override;

    void syncFromState(const state_t &s) override;
    NavIntent onEvent(const Event &e) override;
    void announce() override;

    Screen &screen() override
    {
        return screen_;
    }

    /**
     * Check whether a frequency entry is open.
     *
     * @return true while the RX or TX digits are being typed.
     */
    bool freqEntryOpen() const
    {
        return (entry_ != nullptr) && !entry_->destination;
    }

private:
    void syncMode(const channel_t &ch);
    void syncMeter(const state_t &s);
    void announceVfoState(); //< speak channel summary (MEM) or freq+mode (VFO)

    /* --- interaction --- */
    NavIntent
    onInputEvent(const Event &e); //< key handling while entering a freq

    /**
     * Step the frequency in VFO mode, the channel in memory mode.
     *
     * @param dir: +1 up, -1 down.
     */
    void step(int dir);
    void stepFreq(int dir);          //< VFO: tune RX+TX by one freq step
    void stepChannel(int dir);       //< MEM: load the next/prev channel
    void toggleVfoMem();             //< switch between VFO and memory mode
    bool loadChannel(int16_t index); //< bank-aware channel load into state

    /**
     * Open an entry, allocating its state.
     *
     * @param destination: edit the destination rather than the frequency.
     * @return false if the entry could not be allocated.
     */
    bool openEntry(bool destination);

    /** Close the entry and free its state. */
    void closeEntry();

    /**
     * Open a frequency entry with the RX/TX slot fields pre-filled from state.
     *
     * @return false if the entry could not be allocated.
     */
    bool openInput();
    void beginInput(uint8_t firstDigit); //< open entry with a digit (type-over)
    void beginInputTweak(); //< open entry to edit the current freq in place (*)
    void switchField();     //< toggle RX/TX entry, deriving TX from RX + offset
    void deriveTx();        //< fill the TX field with the entered RX + offset
    void confirmInput();    //< ENTER: validate + commit both RX and TX in band
    void exitInput();       //< leave keypad entry, restore the readout
    void refreshInput();    //< redraw the hero/label from the entry

    /**
     * Edit the RX or the TX digits, keeping both buffers' digits.
     *
     * @param tx: edit TX rather than RX.
     * @param slot: cursor slot.
     */
    void bindField(bool tx, uint8_t slot);
    void announceFreq(); //< speak the current channel frequencies
    void clearFmTone();  //< drop the momentary FM tone if latched

    /* --- M17 destination (shown in the channel line, edited via #) --- */
    void composeChanLine(const state_t &s, char *idxOut, char *nameOut,
                         uint16_t nameSz); //< build the index + name slot text
    void m17DstLabel(char *out, uint16_t sz); //< "@<dest>" (or "@ALL")

    /**
     * Handle an event while the destination is edited.
     *
     * @param e: event.
     * @return the navigation intent.
     */
    NavIntent onDstEditEvent(const Event &e);

    /** Open the destination editor on the stored destination. */
    void beginDstEdit();

    /**
     * Close the destination editor.
     *
     * @param commit: store the edited destination rather than drop it.
     */
    void endDstEdit(bool commit);

    /**
     * Bind the entry's field to its destination buffer.
     *
     * @param cursor: cursor position.
     */
    void bindDst(uint16_t cursor);

    /**
     * Give the destination the index slot while it is edited, on the compact
     * screen where the two share a row.
     *
     * @param editing: the destination is being edited.
     */
    void showDstEdit(bool editing);

    /** Show "@W1[A]W" with the bracketed cursor in the channel line. */
    void renderDstEdit();

    /** Speak the character under the cursor. */
    void announceDstChar();

    /* Frequency keypad entry (mirrors the classic MAIN_VFO_INPUT screen), now on
     * the shared TextInput slot engine: two 7-slot numeric fields (RX then TX,
     * 100 MHz..100 Hz), rendered through FreqHero. RX pre-fills from the current
     * frequency; TX auto-derives from RX plus the channel's existing offset. */
    static constexpr uint8_t kFreqDigits = 7;

    /**
     * A frequency or M17 destination entry (classic MAIN_VFO # -> dst input),
     * edited in place through one TextInput. It exists only while open, so
     * the screen does not carry it the rest of the time.
     */
    struct Entry {
        TextInput input;               ///< The RX or TX digits, or the dest
        long long errorAt = 0;         ///< getTick() of an out-of-band commit
        int64_t shift = 0;             ///< TX - RX at open (repeater offset)
        char rx[kFreqDigits + 1] = {}; ///< RX digits
        char tx[kFreqDigits + 1] = {}; ///< TX digits
        char dst[10] = {};             ///< Destination, as settings.m17_dest
        uint8_t rxSlot = 0;            ///< RX cursor slot while TX is edited
        uint8_t txSlot = 0;            ///< TX cursor slot while RX is edited
        bool destination = false;      ///< Editing the dest, not the freq
        bool txField = false;          ///< Editing TX rather than RX
        bool rxPristine = true;        ///< RX untouched: a digit retypes it
        bool txPristine = true;        ///< TX still derived from RX + offset
    };

    Screen screen_;

    Flex root_;
    FreqHero hero_;
    Flex nameRow_; ///< Name headline; compact: index + name + detail
    Flex metaRow_; ///< Regular only: index (left) + CAN / tone (right)
    Label chanIdx_;
    Label chanName_;
    TextRow chanDetail_; //< right-aligned CAN (M17) / tone (FM)
    SignalMeter meter_;
    Flex spacer_;

    /* Channel-line slot: composed index + name text, change-gated by string
     * compare (the name slot depends on tuner mode, channel name, radio mode
     * and, in M17, the destination — a plain string diff covers them all). */
    char idxCache_[8] = { 1, 0 };   //< sentinel forces the first paint
    char nameCache_[40] = { 1, 0 }; //< composed name, or bracket-cursor dest
    char readoutBuf_[12] = { 0 };
    char modeSub_[8] = { 0 }; //< mode-stack 2nd line: PL tone (FM) / CAN (M17)
    const char *shownMode_ = nullptr; ///< Mode label shown, nullptr to repaint
    const char *shownTag_ = "";       ///< CAN / CTCSS tag shown

    uint32_t lastFreq_ = 0xFFFFFFFFu;
    uint8_t lastStatus_ = 0xFFu;
    int32_t lastRssi_ = INT32_MIN;
    uint8_t lastSql_ = 0xFFu;  //< squelch level last drawn on the meter marker
    uint8_t lastMode_ = 0xFFu; //< channel mode (marker only shows in FM)
    uint16_t lastFill_ = UINT16_MAX; ///< Meter fill width last drawn

    Entry *entry_ = nullptr; ///< Open entry, on the heap; nullptr if none
};

} // namespace ortxui

#endif /* ORTX_UI_VFOVIEW_HPP */
