/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_TUNER_HPP
#define ORTX_UI_TUNER_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>
#include "layout/Flex.hpp"
#include "widgets/BarRow.hpp"
#include "widgets/FreqHero.hpp"
#include "widgets/Label.hpp"
#include "widgets/TextRow.hpp"
#include "widgets/TunerState.hpp"

namespace ortxui
{

/**
 * One tuner: the frequency and mode, the channel line and the signal meter,
 * drawn from a TunerState. It repaints only what changed since the last
 * update(). While the screen edits the frequency or the M17 destination, the
 * entry is shown in place of the readout it edits.
 *
 * On a screen of several tuners each one is a card with its number. The
 * selected card has an accent border and number; a receiving card shows an
 * RX badge and its frequency in the receive colour, a transmitting one a TX
 * badge in the transmit colour. A placeholder shows dashes.
 */
class Tuner : public Flex
{
public:
    /**
     * How the tuner is laid out.
     */
    enum class Style : uint8_t {
        Compact, ///< Index, name and detail on one row
        Regular, ///< The name on a row of its own, then index and detail
        Card,    ///< As Regular, in a card with the number and the badges
    };

    /** Constructor. */
    Tuner() = default;

    /** Destructor. */
    ~Tuner() = default;

    /**
     * Set up the children. The screen places the tuner and lays it out.
     *
     * @param style: layout.
     * @param number: tuner number shown on a card, from 1.
     */
    void setup(Style style, uint8_t number);

    /**
     * Paint the card, its number and badges, on the Card style.
     *
     * @param d: drawing context.
     */
    void draw(DrawCtx &d) override;

    /**
     * Follow the model.
     *
     * @param t: what the tuner shows.
     */
    void update(const TunerState &t);

    /**
     * Show a frequency entry in place of the frequency and the channel line.
     *
     * @param main: MHz and kHz typed, as "146.5--".
     * @param sub: sub-kHz digits typed, as "00" or "--".
     * @param tx: the TX frequency is typed rather than the RX one.
     * @param error: the entry is out of band.
     */
    void showFreqEntry(const char *main, const char *sub, bool tx, bool error);

    /**
     * Show a destination entry in place of the channel line.
     *
     * @param text: destination with its cursor, as "@W1[A]W".
     */
    void showDestEntry(const char *text);

    /** Show the readout again once an entry closes. */
    void endEntry();

    /**
     * Get the font of the channel name, which a destination entry is shown in.
     *
     * @return the font.
     */
    fontSize_t nameFont() const
    {
        return chanName.font();
    }

    /**
     * Get the width of the channel name, which a destination entry fits in.
     *
     * @return the width in pixels.
     */
    uint16_t nameWidth() const
    {
        return chanName.area().w;
    }

private:
    /**
     * What an open entry shows in place of the readout.
     */
    enum class Entry : uint8_t {
        None,        ///< No entry: the readout shows
        Frequency,   ///< The frequency and the channel line
        Destination, ///< The channel line
    };

    /**
     * Follow the frequency.
     *
     * @param t: model.
     */
    void updateFreq(const TunerState &t);

    /**
     * Follow the mode label and the CAN or CTCSS detail.
     *
     * @param t: model.
     */
    void updateMode(const TunerState &t);

    /**
     * Follow the channel line.
     *
     * @param t: model.
     */
    void updateLine(const TunerState &t);

    /**
     * Follow the meter: RSSI on an S-scale with the squelch mark while
     * receiving, power while transmitting.
     *
     * @param t: model.
     */
    void updateMeter(const TunerState &t);

    /**
     * Give the destination entry the index slot, on the compact layout where
     * the two share a row.
     *
     * @param editing: the destination is being edited.
     */
    void showIndex(bool editing);

    /** Repaint the change-gated readout at the next update(). */
    void invalidateReadout();

    /**
     * Get the colour of the frequency on a card: the receive or transmit
     * colour while active, muted on a placeholder.
     *
     * @return the colour role.
     */
    Sem freqColor() const;

    /** Flags of the card state, as in cardFlags. */
    enum CardFlag : uint8_t {
        CardSelected = 1u << 0,     ///< Keys act on this tuner
        CardReceiving = 1u << 1,    ///< The squelch is open
        CardTransmitting = 1u << 2, ///< The radio transmits on it
        CardPlaceholder = 1u << 3,  ///< No receiver behind it yet
    };

    FreqHero hero;      ///< Frequency and mode label
    Flex nameRow;       ///< Name headline; compact: index + name + detail
    Flex metaRow;       ///< Regular only: index (left) + CAN / tone (right)
    Label chanIdx;      ///< "VFO" or the memory index
    Label chanName;     ///< Channel name or M17 destination
    TextRow chanDetail; ///< CAN (M17) or CTCSS tone (FM), right-aligned
    BarRow meter;       ///< RSSI or TX power
    Flex spacer;        ///< Pushes the meter to the bottom

    const char *shownMode = nullptr; ///< Mode label shown, nullptr to repaint
    const char *shownTag = "";       ///< CAN / CTCSS tag shown
    uint32_t lastFreq = UINT32_MAX;  ///< Frequency shown
    int32_t lastRssi = INT32_MIN;    ///< RSSI on the meter
    uint16_t lastFill = UINT16_MAX;  ///< Meter fill width shown
    char idxCache[sizeof(TunerState::index)] = {}; ///< Index shown
    char nameCache[sizeof(TunerState::name)] = {}; ///< Name shown
    char readoutBuf[12] = {};                      ///< Meter readout
    char modeSub[sizeof(TunerState::detail)] = {}; ///< Detail value shown
    uint8_t lastSql = 0xFFu;      ///< Squelch level on the meter mark
    uint8_t lastMode = 0xFFu;     ///< Mode on the meter: FM shows the mark
    uint8_t lastTx = 0xFFu;       ///< Meter shows RX (0), TX (1) or nothing (2)
    uint8_t cardFlags = 0;        ///< CardFlag set shown
    uint8_t number = 1;           ///< Tuner number shown on a card
    Style style = Style::Regular; ///< Layout
    Entry entry = Entry::None;    ///< Entry shown in place of the readout
};

} // namespace ortxui

#endif /* ORTX_UI_TUNER_HPP */
