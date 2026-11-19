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
 * A labelled horizontal level bar: an optional leading label, a track filled
 * to a fraction in [0, 1], and an optional trailing readout. The volume HUD
 * and the macro menu's squelch use it, so the two look alike.
 */
class BarRow : public Object
{
public:
    /** Constructor. */
    BarRow() = default;

    /** Destructor. */
    ~BarRow() = default;

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
    /** Set the trailing readout, borrowed; nullptr for none. */
    void setReadout(const char *r)
    {
        readout_ = (r != nullptr) ? r : "";
    }

    /** Set the filled fraction of the track, clamped to [0, 1] when drawn. */
    void setValue(float v)
    {
        value_ = v;
    }

    /** Set the font of the label and the readout. */
    void setFont(fontSize_t f)
    {
        font_ = f;
    }

    /** Set the colour roles of the track and its fill. */
    void setColors(Sem track, Sem fill)
    {
        track_ = track;
        fill_ = fill;
    }

    /** Set the colour role of the label and the readout. */
    void setTextColor(Sem c)
    {
        text_ = c;
    }

    /** No width of its own, the font's line height: the layout sizes it. */
    Size natural() const override;

    /** Paint the label, the track and the readout. */
    void draw(DrawCtx &d) override;

private:
    UiText label_;                    ///< Leading label
    const char *icon_ = nullptr;      ///< Glyph before the label, if any
    const char *readout_ = "";        ///< Trailing readout
    float value_ = 0.0f;              ///< Filled fraction
    fontSize_t font_ = FONT_SIZE_6PT; ///< Text font
    Sem track_ = Sem::SurfaceHigh;    ///< Track colour role
    Sem fill_ = Sem::Primary;         ///< Fill colour role
    Sem text_ = Sem::OnSurface;       ///< Text colour role
};

} // namespace ortxui

#endif /* ORTX_UI_BARROW_HPP */
