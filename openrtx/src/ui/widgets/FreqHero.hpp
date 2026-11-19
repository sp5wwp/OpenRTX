/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_FREQHERO_HPP
#define ORTX_UI_FREQHERO_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>
#include "base/Object.hpp"
#include "style/SemanticColor.hpp"

namespace ortxui
{

/**
 * The VFO frequency: a large frequency to kHz, small sub-kHz digits and the
 * mode label (WFM/NFM/M17) right-aligned, all on one baseline. It draws itself
 * because the box layout cannot align mixed font sizes on a baseline.
 */
class FreqHero : public Object
{
public:
    /** Constructor. */
    FreqHero() = default;

    /** Destructor. */
    ~FreqHero() = default;

    /**
     * Show a frequency, in the readout colour.
     *
     * @param hz: frequency in Hz.
     */
    void setFreq(uint32_t hz);

    /**
     * Show a keypad entry instead of a frequency, '-' marking the digits not
     * typed yet.
     *
     * @param main: MHz and kHz, as "146.5--".
     * @param sub: sub-kHz digits, as "00" or "--".
     */
    void setEntry(const char *main, const char *sub);

    /**
     * Set the mode label.
     *
     * @param mode: label, which must outlive the widget's use of it.
     */
    void setMode(const char *mode)
    {
        modeText = mode;
    }

    /**
     * Set the colour of the frequency, not of the mode label: the accent while
     * editing, the alert colour for an out-of-band entry. setFreq() resets it.
     *
     * @param c: colour role.
     */
    void setTextColor(Sem c)
    {
        textColor = c;
    }

    /** Paint the frequency, the sub-kHz digits and the mode label. */
    void draw(DrawCtx &d) override;

private:
    const char *modeText = "";      ///< Mode label
    char mainBuf[16] = "0.000";     ///< MHz and kHz
    char subBuf[6] = "00";          ///< Sub-kHz digits
    Sem textColor = Sem::OnSurface; ///< Colour of the frequency
};

} // namespace ortxui

#endif /* ORTX_UI_FREQHERO_HPP */
