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
 * The VFO signal meter: a leading label ("RX"/"TX"), a continuous horizontal
 * bar filled to `value` (RSSI in RX, power in TX), an optional red threshold
 * mark (the squelch opening level) on the same axis, an optional S-scale
 * (1/3/5/7/9) beneath it, and a trailing readout. Drawn within its
 * layout-assigned area.
 *
 * The bar is pixel-resolution: unlike a fixed set of dots it does not quantise
 * RSSI or the squelch mark to whole S-points, so the full granularity of both
 * is visible and they share one axis (a green level under a red squelch gate).
 */
class SignalMeter : public Object
{
public:
    void setLabel(const char *l)
    {
        label_ = (l != nullptr) ? l : "";
    }
    void setReadout(const char *r)
    {
        readout_ = (r != nullptr) ? r : "";
    }
    void setColor(Sem c)
    {
        color_ = c;
    }
    /** Main fill fraction (RSSI or power), clamped to [0, 1]. */
    void setValue(float v)
    {
        value_ = v;
    }
    /**
     * Threshold mark (squelch opening level) as a fraction [0, 1] on the same
     * axis as the fill, drawn as a red vertical gate. A negative value (the
     * default) hides it.
     */
    void setMarker(float v)
    {
        marker_ = v;
    }
    void setShowScale(bool s)
    {
        showScale_ = s;
    }

    Size natural() const override;
    void draw(DrawCtx &d) override;

private:
    const char *label_ = "";
    const char *readout_ = "";
    Sem color_ = Sem::RxSuccess;
    float value_ = 0.0f;
    float marker_ = -1.0f; //< squelch mark fraction, <0 hides it
    bool showScale_ = true;
};

} // namespace ortxui

#endif /* ORTX_UI_SIGNALMETER_HPP */
