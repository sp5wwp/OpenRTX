/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_FREQHERO_HPP
#define ORTX_UI_FREQHERO_HPP

#include <cstdint>
#include "core/Object.hpp"
#include "style/SemanticColor.hpp"

namespace ortxui
{

/**
 * The VFO frequency hero (mockups 5-6): a large left-aligned frequency to
 * kHz, small trailing sub-kHz digits sharing its baseline, and the mode /
 * bandwidth label (WFM/NFM/M17) right-aligned on that same baseline. Custom-
 * drawn because the mixed-size baseline alignment is finer than the box layout
 * expresses. Secondary M17/FM details (CAN, tone) live in the channel row
 * below, not here. The mode label is a borrowed const string.
 */
class FreqHero : public Object
{
public:
    void setFreq(uint32_t hz);

    /** Keypad-entry rendering: the caller composes the main ("146.5--") and sub
     *  ("00" / "--") strings itself, with '-' placeholders for un-entered slots,
     *  and drives the colour with setTextColor(). Bypasses the numeric format of
     *  setFreq() so partial entry and placeholders show through. */
    void setEntry(const char *main, const char *sub);

    /** Right-aligned mode/bandwidth label (WFM/NFM/M17), on the frequency
     *  baseline. Power is intentionally omitted — it is shown on the meter
     *  while transmitting. */
    void setMode(const char *mode)
    {
        mode_ = (mode != nullptr) ? mode : "";
    }

    /** Colour of the frequency text (not the mode label). Gold while editing,
     *  the alert colour on an out-of-band entry; setFreq() resets it. */
    void setTextColor(Sem c)
    {
        textColor_ = c;
    }

    void draw(DrawCtx &d) override;

private:
    char mainBuf_[16] = "0.000";
    char subBuf_[6] = "00";
    const char *mode_ = "";
    Sem textColor_ = Sem::OnSurface;
};

} // namespace ortxui

#endif /* ORTX_UI_FREQHERO_HPP */
