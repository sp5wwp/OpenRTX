/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/Tuner.hpp"
#include "base/UiFormat.hpp"
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

void Tuner::setup(Style s)
{
    style = s;
    const bool regular = (style == Style::Regular);

    setAxis(Axis::Column);
    setGrow(1);

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

    addChild(&hero);

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

        addChild(&nameRow);
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
    /* Show the frequency the radio is on: TX while transmitting. */
    const uint32_t shown = (t.transmitting && (t.txFreq != t.rxFreq)) ?
                               t.txFreq :
                               t.rxFreq;
    if (shown == lastFreq)
        return;

    hero.setFreq(shown);
    hero.invalidate();
    lastFreq = shown;
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
    const uint8_t tx = t.transmitting ? 1u : 0u;
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
