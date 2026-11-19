/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_BARROW_HPP
#define ORTX_UI_BARROW_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>
#include "base/Object.hpp"
#include "base/UiText.hpp"
#include "core/graphics.h"
#include "style/SemanticColor.hpp"

namespace ortxui
{

/**
 * A labelled horizontal level bar: an optional leading label, a track filled
 * to a fraction in [0, 1], an optional trailing readout, and optionally a mark
 * on the track and an S-scale beneath it. The VFO signal meter, the volume HUD
 * and the macro menu's squelch use it, so they look alike.
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

    /**
     * Set the trailing readout.
     *
     * @param r: readout, borrowed; nullptr for none.
     */
    void setReadout(const char *r)
    {
        readout_ = (r != nullptr) ? r : "";
    }

    /**
     * Set the fill.
     *
     * @param v: fraction of the track, clamped to [0, 1] when drawn.
     */
    void setValue(float v)
    {
        value_ = v;
    }

    /**
     * Set the mark.
     *
     * @param v: fraction on the track's axis; negative hides the mark.
     */
    void setMarker(float v)
    {
        marker_ = v;
    }

    /**
     * Show or hide the S-scale beneath the track.
     *
     * @param s: whether the scale shows.
     */
    void setShowScale(bool s)
    {
        showScale_ = s;
    }

    /**
     * Set the font of the label and the readout.
     *
     * @param f: font size.
     */
    void setFont(fontSize_t f)
    {
        font_ = f;
    }

    /**
     * Set the thickness of the track.
     *
     * @param h: thickness in pixels.
     */
    void setBarHeight(uint8_t h)
    {
        barH_ = h;
    }

    /**
     * Set the colour roles of the track and its fill.
     *
     * @param track: track colour role.
     * @param fill: fill colour role.
     */
    void setColors(Sem track, Sem fill)
    {
        track_ = track;
        fill_ = fill;
    }

    /**
     * Set the colour role of the label and the readout.
     *
     * @param c: colour role.
     */
    void setTextColor(Sem c)
    {
        text_ = c;
    }

    /**
     * Get the width of the fill, which follows the value, the area and the
     * space the label and the readout take.
     *
     * @return the width in pixels.
     */
    uint16_t fillWidth() const;

    /**
     * Get the natural size: no width of its own, so the layout sizes it, and
     * the height of a text line, plus the scale when it shows.
     *
     * @return the size.
     */
    Size natural() const override;

    /** Paint the label, the track, the scale, the mark and the readout. */
    void draw(DrawCtx &d) override;

private:
    /**
     * Compose the label, joined to its glyph.
     *
     * @param buf: storage for the joined label.
     * @param n: size of buf.
     * @return the label.
     */
    const char *labelText(char *buf, size_t n) const;

    /**
     * Get the track's span: the area inside the gutters, less the label and
     * the readout.
     *
     * @param left: set to the first column of the track.
     * @return the track width, 0 if there is no room.
     */
    uint16_t track(int16_t &left) const;

    UiText label_;                    ///< Leading label
    const char *icon_ = nullptr;      ///< Glyph before the label, if any
    const char *readout_ = "";        ///< Trailing readout
    float value_ = 0.0f;              ///< Filled fraction
    float marker_ = -1.0f;            ///< Mark fraction, negative to hide it
    fontSize_t font_ = FONT_SIZE_6PT; ///< Text font
    Sem track_ = Sem::SurfaceHigh;    ///< Track colour role
    Sem fill_ = Sem::Primary;         ///< Fill colour role
    Sem text_ = Sem::OnSurface;       ///< Text colour role
    uint8_t barH_ = 8;                ///< Track thickness
    bool showScale_ = false;          ///< The S-scale shows
};

} // namespace ortxui

#endif /* ORTX_UI_BARROW_HPP */
