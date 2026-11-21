/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/Tuner.hpp"
#include "base/UiFormat.hpp"
#include "render/DrawCtx.hpp"
#include "core/graphics.h"
#include "core/utils.h"
#include "rtx/rtx.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace ortxui
{

/** Thickness of the meter's bar. */
static constexpr uint8_t METER_BAR_H = 7;

/** Range of the TX meter's dB scale, which is full at POWER_MAX_MW. */
static constexpr float POWER_SPAN_DB = 10.0f;

/** Whether a card is laid out for the large screen, in larger fonts. */
static constexpr bool LARGE = (sizeClass() >= SizeClass::Large);

static constexpr int16_t CARD_INSET_X = 3; ///< Card inset from the sides
static constexpr int16_t CARD_INSET_Y = 2; ///< Card inset, top and bottom
static constexpr uint16_t CARD_RADIUS = 6; ///< Card corner radius
static constexpr uint16_t CARD_BORDER = 2; ///< Border of the selected card
static constexpr uint8_t CARD_PAD_X = 8;   ///< Content inset from the sides
static constexpr uint8_t CARD_PAD_Y = 6;   ///< Content inset, top and bottom

/** Side slots of a card's name row, for the number and the RX/TX badge. */
static constexpr uint8_t HEADER_SIDE = LARGE ? 32 : 20;

static constexpr uint16_t BADGE_W = LARGE ? 26 : 16; ///< Number and RX badge
static constexpr uint16_t BADGE_RADIUS = 4;          ///< Badge corner radius

/** Font of the number and the RX/TX badge. */
static constexpr fontSize_t BADGE_FONT = LARGE ? FONT_SIZE_8PT : FONT_SIZE_6PT;

/** Frequency of a placeholder, and its sub-kHz digits. */
static constexpr char PLACEHOLDER_FREQ[] = "---.---";
static constexpr char PLACEHOLDER_SUB[] = "--";

/** Meter style of a placeholder, after RX (0) and TX (1). */
static constexpr uint8_t METER_IDLE = 2;

/**
 * Get the meter fill fraction for a TX power on the dB scale.
 *
 * @param mw: power in mW.
 * @return the fraction, below 0 under the scale; the meter clamps it.
 */
static float powerToFrac(uint32_t mw)
{
    if (mw == 0u)
        return 0.0f;
    const float db =
        10.0f
        * std::log10(static_cast<float>(mw) / static_cast<float>(POWER_MAX_MW));
    return 1.0f + db / POWER_SPAN_DB;
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
    return s / 11.0f;
}

void Tuner::setup(Style s, uint8_t num)
{
    style = s;
    number = num;
    const bool card = (style == Style::Card);
    const bool regular = (style != Style::Compact);

    setAxis(Axis::Column);
    setGrow(1);
    if (card)
        setPadding(CARD_PAD_X, CARD_PAD_Y);

    /* Taller than the text on the roomy screen; just clearing it on the
     * compact one, so the channel line and the meter fit in 64 px. A card
     * on the large screen fits three to a screen. */
    hero.setBasis((card && LARGE) ? 30 : (regular ? 40 : 26));

    /* The channel line in one font; rows a full line high, so descenders and
     * the '@' of a destination are not clipped. A card's name is larger. */
    const fontSize_t chanFont = regular ? FONT_SIZE_8PT : FONT_SIZE_6PT;
    const fontSize_t nameFont = (card && LARGE) ? FONT_SIZE_9PT : chanFont;
    const int16_t rowH = gfx_getFontLineHeight(chanFont);

    /* "VFO" or the 1-based memory index, wide enough for "VFO". */
    chanIdx.setFont(chanFont);
    chanIdx.setAlign(TEXT_ALIGN_LEFT);
    chanIdx.setColor(Sem::OnSurfaceMuted);
    chanIdx.setBasis(regular ? 34 : 26);

    /* Channel name or M17 destination; on the roomy screen on a line of its
     * own, so a long destination shows in full. */
    chanName.setFont(nameFont);
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
    meter.setBasis((card && LARGE) ? 21 : (regular ? 19 : 15));
    if (card && LARGE)
        meter.setFont(FONT_SIZE_8PT);
    meter.setBarHeight(METER_BAR_H);
    meter.setColors(Sem::SurfaceHigh, Sem::RxSuccess);
    meter.setTextColor(Sem::RxSuccess);
    meter.setShowScale(true);
    meter.setLabel(STR(RX));
    spacer.setGrow(1);

    if (card) {
        /* The name on top, between the number and the RX/TX badge. */
        nameRow.setAxis(Axis::Row);
        nameRow.setAlign(Align::Center);
        nameRow.setJustify(Justify::Start);
        nameRow.setPadding(HEADER_SIDE, 0);
        nameRow.setBasis(gfx_getFontLineHeight(nameFont));
        nameRow.addChild(&chanName);
        addChild(&nameRow);
    }

    addChild(&hero);

    if (regular) {
        /* Roomy layout: the name on its own line, then the index (left) and
         * the CAN / tone detail (right). */
        if (!card) {
            nameRow.setAxis(Axis::Row);
            nameRow.setAlign(Align::Center);
            nameRow.setJustify(Justify::Start);
            nameRow.setPadding(4, 0);
            nameRow.setBasis(rowH);
            nameRow.addChild(&chanName);
            addChild(&nameRow);
        }

        metaRow.setAxis(Axis::Row);
        metaRow.setAlign(Align::Center);
        metaRow.setJustify(Justify::SpaceBetween);
        metaRow.setPadding(4, 0);
        metaRow.setBasis(rowH);
        metaRow.addChild(&chanIdx);
        metaRow.addChild(&chanDetail);
        addChild(&metaRow);
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

        addChild(&nameRow);
    }

    addChild(&spacer);
    addChild(&meter);

    invalidateReadout();
}

void Tuner::update(const TunerState &t)
{
    /* A card shows the selection and activity: repaint it, and the frequency
     * in the colour of the activity, when they change. */
    uint8_t flags = 0;
    if (t.selected)
        flags |= CardSelected;
    if (t.receiving)
        flags |= CardReceiving;
    if (t.transmitting)
        flags |= CardTransmitting;
    if (t.placeholder)
        flags |= CardPlaceholder;
    if (flags != cardFlags) {
        cardFlags = flags;
        if (style == Style::Card) {
            lastFreq = UINT32_MAX;
            invalidate();
        }
    }

    /* A frequency entry owns the frequency and the channel line, a
     * destination entry the channel line. */
    if (entry != Entry::Frequency) {
        updateFreq(t);
        updateMode(t);
    }
    if (entry == Entry::None)
        updateLine(t);

    updateMeter(t);
}

void Tuner::updateFreq(const TunerState &t)
{
    /* Show the frequency the radio is on: TX while transmitting. A
     * placeholder has none, so 0 stands for its dashes. */
    const uint32_t shown = t.placeholder ? 0u :
                           (t.transmitting && (t.txFreq != t.rxFreq)) ?
                                           t.txFreq :
                                           t.rxFreq;
    if (shown == lastFreq)
        return;

    if (t.placeholder)
        hero.setEntry(PLACEHOLDER_FREQ, PLACEHOLDER_SUB);
    else
        hero.setFreq(shown);
    if (style == Style::Card)
        hero.setTextColor(freqColor());
    hero.invalidate();
    lastFreq = shown;
}

Sem Tuner::freqColor() const
{
    if ((cardFlags & CardPlaceholder) != 0u)
        return Sem::OnSurfaceMuted;
    if ((cardFlags & CardTransmitting) != 0u)
        return Sem::TxDanger;
    if ((cardFlags & CardReceiving) != 0u)
        return Sem::RxSuccess;
    return Sem::OnSurface;
}

void Tuner::updateMode(const TunerState &t)
{
    /* The texts are literals or table entries, so the pointers kept stay
     * valid. */
    const bool same = (shownMode != nullptr) && (strcmp(t.mode, shownMode) == 0)
                   && (strcmp(t.detailTag, shownTag) == 0)
                   && (strcmp(t.detail, modeSub) == 0);
    if (same)
        return;
    shownMode = t.mode;
    shownTag = t.detailTag;
    memcpy(modeSub, t.detail, sizeof(modeSub));

    hero.setMode(shownMode);
    hero.invalidate();
    chanDetail.setText(0, shownTag);
    chanDetail.setText(1, modeSub);
    chanDetail.invalidate();
}

void Tuner::updateLine(const TunerState &t)
{
    if (strncmp(t.index, idxCache, sizeof(idxCache)) != 0) {
        memcpy(idxCache, t.index, sizeof(idxCache));
        chanIdx.setText(idxCache);
        chanIdx.invalidate();
    }
    if (strncmp(t.name, nameCache, sizeof(nameCache)) != 0) {
        memcpy(nameCache, t.name, sizeof(nameCache));
        chanName.setText(nameCache);
        chanName.invalidate();
    }
}

void Tuner::updateMeter(const TunerState &t)
{
    /* A placeholder's meter is an empty track, set once. */
    const uint8_t tx = t.placeholder ? METER_IDLE : (t.transmitting ? 1u : 0u);
    if (t.placeholder) {
        if (lastTx != METER_IDLE) {
            lastTx = METER_IDLE;
            meter.setColors(Sem::SurfaceHigh, Sem::SurfaceHigh);
            meter.setTextColor(Sem::OnSurfaceMuted);
            meter.setMarker(-1.0f);
            meter.setValue(0.0f);
            meter.setReadout(nullptr);
            meter.invalidate();
        }
        return;
    }

    const bool restyle = (tx != lastTx) || (t.squelch != lastSql)
                      || (t.opMode != lastMode);
    if (!restyle && (t.rssi == lastRssi))
        return;

    lastTx = tx;
    lastRssi = t.rssi;
    lastSql = t.squelch;
    lastMode = t.opMode;

    char readout[sizeof(readoutBuf)];
    if (t.transmitting) {
        if (restyle) {
            meter.setLabel(STR(TX));
            meter.setColors(Sem::SurfaceHigh, Sem::TxDanger);
            meter.setTextColor(Sem::TxDanger);
            meter.setShowScale(false);
            meter.setMarker(-1.0f); /* squelch gates RX only */
        }
        meter.setValue(powerToFrac(t.txPower));

        formatPower(t.txPower, readout, sizeof(readout));
    } else {
        const uint8_t level = rssiToSlevel(t.rssi);

        if (restyle) {
            meter.setLabel(STR(RX));
            meter.setColors(Sem::SurfaceHigh, Sem::RxSuccess);
            meter.setTextColor(Sem::RxSuccess);
            meter.setShowScale(true);

            /* The mark sits where the signal has to reach; only FM gates on
             * RSSI. */
            if (t.opMode == OPMODE_FM)
                meter.setMarker(
                    rssiToFrac(static_cast<float>(squelchToRssi(t.squelch))));
            else
                meter.setMarker(-1.0f);
        }
        meter.setValue(rssiToFrac(static_cast<float>(t.rssi)));

        if (level > 9u)
            sniprintf(readout, sizeof(readout), "+%u",
                      static_cast<unsigned>((level - 9u) * 10u));
        else
            sniprintf(readout, sizeof(readout), "S%u",
                      static_cast<unsigned>(level));
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

void Tuner::showFreqEntry(const char *main, const char *sub, bool tx,
                          bool error)
{
    entry = Entry::Frequency;

    hero.setEntry(main, sub);
    hero.setMode((tx ? STR(TX) : STR(RX)).str());
    hero.setTextColor(error ? Sem::Mark : Sem::Accent);
    hero.invalidate();

    chanIdx.setText("");
    chanName.setText(error ? STR(outOfBand) :
                             (tx ? STR(enterTx) : STR(enterRx)));
    chanDetail.setText(0, "");
    chanDetail.setText(1, "");
    chanIdx.invalidate();
    chanName.invalidate();
    chanDetail.invalidate();
}

void Tuner::showDestEntry(const char *text)
{
    if (entry != Entry::Destination) {
        entry = Entry::Destination;
        showIndex(true);
    }

    sniprintf(nameCache, sizeof(nameCache), "%s", text);
    idxCache[0] = '\0';
    chanIdx.setText(idxCache);
    chanName.setText(nameCache);
    chanIdx.invalidate();
    chanName.invalidate();
}

void Tuner::endEntry()
{
    if (entry == Entry::Destination)
        showIndex(false);

    entry = Entry::None;
    invalidateReadout();
}

void Tuner::draw(DrawCtx &d)
{
    if (style != Style::Card) {
        Flex::draw(d);
        return;
    }

    /* The card, with an accent border when selected. */
    const Rect card = { static_cast<int16_t>(bounds.x + CARD_INSET_X),
                        static_cast<int16_t>(bounds.y + CARD_INSET_Y),
                        static_cast<uint16_t>(bounds.w - 2 * CARD_INSET_X),
                        static_cast<uint16_t>(bounds.h - 2 * CARD_INSET_Y) };
    const bool selected = ((cardFlags & CardSelected) != 0u);
    if (selected) {
        d.fillRoundRect(card, CARD_RADIUS, Sem::Accent);
        const Rect inner = { static_cast<int16_t>(card.x + CARD_BORDER),
                             static_cast<int16_t>(card.y + CARD_BORDER),
                             static_cast<uint16_t>(card.w - 2 * CARD_BORDER),
                             static_cast<uint16_t>(card.h - 2 * CARD_BORDER) };
        d.fillRoundRect(inner, CARD_RADIUS - CARD_BORDER, Sem::Surface);
    } else {
        d.fillRoundRect(card, CARD_RADIUS, Sem::Surface);
    }

    /* The number at the start of the name row, filled when selected. */
    const Rect row = nameRow.area();
    const uint16_t badgeH = gfx_getFontLineHeight(BADGE_FONT);
    const int16_t badgeY = static_cast<int16_t>(row.y + (row.h - badgeH) / 2);
    const Rect num = { row.x, badgeY, BADGE_W, badgeH };
    char text[4];
    sniprintf(text, sizeof(text), "%u", static_cast<unsigned>(number));
    d.fillRoundRect(num, BADGE_RADIUS,
                    selected ? Sem::Accent : Sem::SurfaceHigh);
    d.textInBox(num, BADGE_FONT, TEXT_ALIGN_CENTER,
                selected ? Sem::OnAccent : Sem::OnSurfaceMuted, text);

    /* The RX or TX badge at the end of the name row while active. */
    const bool txing = ((cardFlags & CardTransmitting) != 0u);
    const bool rxing = ((cardFlags & CardReceiving) != 0u);
    if (txing || rxing) {
        const Rect badge = { static_cast<int16_t>(row.x + row.w - BADGE_W),
                             badgeY, BADGE_W, badgeH };
        d.fillRoundRect(badge, BADGE_RADIUS,
                        txing ? Sem::TxDanger : Sem::RxSuccess);
        d.textInBox(badge, BADGE_FONT, TEXT_ALIGN_CENTER, Sem::OnAccent,
                    (txing ? STR(TX) : STR(RX)).str());
    }
}

void Tuner::showIndex(bool editing)
{
    if (style != Style::Compact)
        return;

    chanIdx.setFlag(FlagHidden, editing);
    nameRow.onLayout();
}

void Tuner::invalidateReadout()
{
    /* No shown text starts with \1, so the next update repaints them all. */
    lastFreq = UINT32_MAX;
    shownMode = nullptr;
    idxCache[0] = '\1';
    nameCache[0] = '\1';
    invalidate();
}

} // namespace ortxui
