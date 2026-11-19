/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_BARROW_HPP
#define ORTX_UI_BARROW_HPP

#include <cstdint>
#include "core/Object.hpp"
#include "core/UiText.hpp"
#include "core/graphics.h"
#include "style/SemanticColor.hpp"

namespace ortxui
{

/**
 * A labelled horizontal level bar: an optional leading label, a filled progress
 * track spanning the middle, and an optional trailing readout. `value` is a
 * fraction clamped to [0, 1]. Drawn within its layout-assigned area, so a
 * container positions it just by sizing its box (same contract as SignalMeter).
 *
 * It is the shared "level control" used both by the transient Volume overlay and
 * the macro-menu squelch row, so the two read identically on screen.
 */
class BarRow : public Object
{
public:
    /**
     * Set the leading label; plain text is borrowed, not copied.
     *
     * @param l: label, empty for none.
     * @param icon: glyph drawn before the label, nullptr for none. The two
     * are joined when drawn, so the label follows the current language.
     */
    void setLabel(const UiText &l, const char *icon = nullptr)
    {
        label_ = l;
        icon_ = icon;
    }
    void setReadout(const char *r)
    {
        readout_ = (r != nullptr) ? r : "";
    }
    void setValue(float v)
    {
        value_ = v;
    }
    void setFont(fontSize_t f)
    {
        font_ = f;
    }
    void setColors(Sem track, Sem fill)
    {
        track_ = track;
        fill_ = fill;
    }
    void setTextColor(Sem c)
    {
        text_ = c;
    }

    Size natural() const override;
    void draw(DrawCtx &d) override;

private:
    UiText label_;
    const char *icon_ = nullptr; ///< Glyph before the label, nullptr if none
    const char *readout_ = "";
    float value_ = 0.0f;
    fontSize_t font_ = FONT_SIZE_6PT;
    Sem track_ = Sem::SurfaceHigh;
    Sem fill_ = Sem::Primary;
    Sem text_ = Sem::OnSurface;
};

} // namespace ortxui

#endif /* ORTX_UI_BARROW_HPP */
