/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_SIGNALMETER_HPP
#define ORTX_UI_SIGNALMETER_HPP

#include <cstdint>
#include "core/Object.hpp"
#include "style/SemanticColor.hpp"

namespace ortxui
{

/**
 * The VFO signal meter: a label ("RX"/"TX"), a bar filled to a fraction (RSSI
 * or power) at pixel resolution, an optional red mark on the same axis (the
 * squelch level), an optional S-scale beneath it, and a readout.
 */
class SignalMeter : public Object
{
public:
    /** Constructor. */
    SignalMeter() = default;

    /** Destructor. */
    ~SignalMeter() = default;

    /**
     * Set the label.
     *
     * @param l: label, which must outlive the meter's use of it.
     */
    void setLabel(const char *l)
    {
        label_ = l;
    }

    /**
     * Set the readout.
     *
     * @param r: readout, which must outlive the meter's use of it.
     */
    void setReadout(const char *r)
    {
        readout_ = r;
    }

    /**
     * Set the colour of the label, readout and fill.
     *
     * @param c: colour role.
     */
    void setColor(Sem c)
    {
        color_ = c;
    }

    /**
     * Set the fill.
     *
     * @param v: fraction, clamped to [0, 1] when drawn.
     */
    void setValue(float v)
    {
        value_ = v;
    }

    /**
     * Set the mark.
     *
     * @param v: fraction on the fill's axis; negative hides the mark.
     */
    void setMarker(float v)
    {
        marker_ = v;
    }

    /**
     * Show or hide the S-scale.
     *
     * @param s: whether the scale shows.
     */
    void setShowScale(bool s)
    {
        showScale_ = s;
    }

    /**
     * Get the width of the bar's fill, which follows the value, the area and
     * the space the label and the readout take.
     *
     * @return the width in pixels.
     */
    uint16_t fillWidth() const;

    /**
     * Get the meter's natural size.
     *
     * @return the size.
     */
    Size natural() const override;

    /** Paint the label, bar, mark, scale and readout. */
    void draw(DrawCtx &d) override;

private:
    /**
     * Get the bar's span: the area inside the gutters, less the label and the
     * readout.
     *
     * @param left: set to the first column of the bar.
     * @return the bar width, 0 if there is no room.
     */
    uint16_t track(int16_t &left) const;

    const char *label_ = "";     ///< Label
    const char *readout_ = "";   ///< Readout
    float value_ = 0.0f;         ///< Fill fraction
    float marker_ = -1.0f;       ///< Mark fraction, negative to hide it
    Sem color_ = Sem::RxSuccess; ///< Colour of the label, readout and fill
    bool showScale_ = true;      ///< The S-scale shows
};

} // namespace ortxui

#endif /* ORTX_UI_SIGNALMETER_HPP */
