/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_TUNERVIEW_HPP
#define ORTX_UI_TUNERVIEW_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include "views/View.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/TextInput.hpp"
#include "widgets/Tuner.hpp"
#include "widgets/TunerState.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The tuner screen, home of the UI. A tuner is what the radio is tuned to: a
 * VFO, tuned freely, or a memory channel. The screen shows its frequency and
 * mode, the channel name or M17 destination, the channel index with the CAN
 * or CTCSS tone, and the signal meter at the bottom. The compact screen puts
 * the index, name and detail on one row.
 *
 * As the navigation root, ENTER opens the main menu. UP/DOWN or the knob tune
 * the frequency in VFO mode or step the channel in memory mode, ESC switches
 * between the two, and a digit opens the frequency entry, RX then TX.
 */
class TunerView : public View
{
public:
    /** Constructor. */
    TunerView() = default;

    /** Destructor. */
    ~TunerView();

    /** Build the widget tree. */
    void build() override;

    /** Keep an open frequency or destination entry while destroyed. */
    void save() const override;

    /** Reopen a frequency or destination entry kept by save(). */
    void restore() override;

    /**
     * Follow the state snapshot.
     *
     * @param s: state snapshot.
     */
    void syncFromState(const state_t &s) override;

    /**
     * Tune, switch VFO and memory mode, or edit an open entry.
     *
     * @param e: event.
     * @return the intent: the main menu on ENTER, else none.
     */
    NavIntent onEvent(const Event &e) override;

    /**
     * Speak the channel summary in memory mode, the frequencies and mode in VFO
     * mode.
     */
    void announce() override;

    /**
     * Get the widget tree.
     *
     * @return the screen.
     */
    Screen &screen() override
    {
        return tree;
    }

    /**
     * Check whether a frequency entry is open.
     *
     * @return true while the RX or TX digits are being typed.
     */
    bool freqEntryOpen() const
    {
        return (entry != nullptr) && !entry->destination;
    }

    /**
     * Describe the tuner the radio is on.
     *
     * @param s: state snapshot.
     * @param opStatus: operating status from the rtx module.
     * @param squelchOpen: the rtx module has the RX squelch open.
     * @param t: model to fill.
     */
    static void describe(const state_t &s, uint8_t opStatus, bool squelchOpen,
                         TunerState &t);

private:
    /**
     * Handle a key while a frequency is being entered.
     *
     * @param e: event.
     * @return no navigation.
     */
    NavIntent onInputEvent(const Event &e);

    /**
     * Step the frequency in VFO mode, the channel in memory mode.
     *
     * @param dir: +1 up, -1 down.
     */
    void step(int dir);

    /**
     * Tune RX and TX by one frequency step, staying in band.
     *
     * @param dir: +1 up, -1 down.
     */
    void stepFreq(int dir);

    /**
     * Load the next or previous channel.
     *
     * @param dir: +1 next, -1 previous.
     */
    void stepChannel(int dir);

    /** Switch between VFO and memory mode. */
    void toggleVfoMem();

    /**
     * Load a channel into the state, through the bank when one is enabled.
     *
     * @param index: channel index, in the bank when one is enabled.
     * @return false if there is no such channel.
     */
    bool loadChannel(int16_t index);

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

    /**
     * Open a frequency entry retyped from a first digit.
     *
     * @param firstDigit: digit typed.
     */
    void beginInput(uint8_t firstDigit);

    /** Open a frequency entry on the current frequency, to edit it in place. */
    void beginInputTweak();

    /** Switch between the RX and TX fields, deriving TX from RX + offset. */
    void switchField();

    /** Fill the TX field with the entered RX plus the offset. */
    void deriveTx();

    /** Advance from RX to TX, or commit both if they are in band. */
    void confirmInput();

    /** Close the frequency entry and show the live readout again. */
    void exitInput();

    /** Show the entry in the frequency and channel line. */
    void refreshInput();

    /**
     * Edit the RX or the TX digits, keeping both buffers' digits.
     *
     * @param tx: edit TX rather than RX.
     * @param slot: cursor slot.
     */
    void bindField(bool tx, uint8_t slot);
    /** Speak the current channel frequencies. */
    void announceFreq();

    /** Drop the FM tone burst if it is keyed. */
    void clearFmTone();

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

    /** Show "@W1[A]W" with the bracketed cursor in the channel line. */
    void renderDstEdit();

    /**
     * Speak a digit typed in the frequency entry.
     *
     * @param d: digit, 0-9.
     */
    void announceDigit(uint8_t d);

    /** Speak the character under the cursor. */
    void announceDstChar();

    /** Digits of a frequency field, 100 MHz to 100 Hz. */
    static constexpr uint8_t FREQ_DIGITS = 7;

    /** The part of an entry that save() keeps while the view is destroyed. */
    struct EntryState {
        long long errorAt = 0;         ///< getTick() of an out-of-band commit
        int32_t shift = 0;             ///< TX - RX at open (repeater offset)
        char rx[FREQ_DIGITS + 1] = {}; ///< RX digits
        char tx[FREQ_DIGITS + 1] = {}; ///< TX digits
        char dst[sizeof(settings_t::m17_dest)] = {}; ///< Destination
        uint8_t rxSlot = 0;       ///< RX cursor slot while TX is edited
        uint8_t txSlot = 0;       ///< TX cursor slot while RX is edited
        uint8_t dstCursor = 0;    ///< Destination cursor, set by save()
        bool destination = false; ///< Editing the dest, not the freq
        bool txField = false;     ///< Editing TX rather than RX
        bool rxPristine = true;   ///< RX untouched: a digit retypes it
        bool txPristine = true;   ///< TX still derived from RX + offset
    };

    /**
     * A frequency or M17 destination entry, edited in place through one
     * TextInput. It exists only while open, so the screen does not carry it
     * the rest of the time.
     */
    struct Entry : EntryState {
        TextInput input; ///< The RX or TX digits, or the destination
    };

    Screen tree;            ///< Widget tree
    Tuner tuner;            ///< The tuner the radio is on
    Entry *entry = nullptr; ///< Open entry, on the heap; nullptr if none
};

} // namespace ortxui

#endif /* ORTX_UI_TUNERVIEW_HPP */
