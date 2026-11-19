/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_LABEL_HPP
#define ORTX_UI_LABEL_HPP

#include "core/Object.hpp"
#include "core/UiText.hpp"
#include "core/graphics.h"
#include "style/SemanticColor.hpp"

namespace ortxui
{

/**
 * A single-line text label drawn within its layout-assigned area. The text is
 * measured and placed relative to the box (horizontally per `align`, vertically
 * centred), so a layout container positions the label just by sizing its area.
 */
class Label : public Object
{
public:
    /** Constructor. */
    Label() = default;

    /** Destructor. */
    ~Label() = default;

    /** How text wider than the label's box is handled. */
    enum class Overflow : uint8_t {
        Clip,      ///< Cut at the box edge
        Ellipsize, ///< Truncated to fit, with a trailing ellipsis
    };

    /**
     * Set the text; plain text is borrowed, not copied.
     *
     * @param t: text.
     */
    void setText(const UiText &t)
    {
        text_ = t;
    }

    /**
     * Get the text.
     *
     * @return the text.
     */
    const UiText &text() const
    {
        return text_;
    }

    /** Set the text colour role. */
    void setColor(Sem c)
    {
        color_ = c;
    }

    /** Set the text font. */
    void setFont(fontSize_t f)
    {
        font_ = f;
    }

    /** Font the text is drawn in. */
    fontSize_t font() const
    {
        return font_;
    }
    /** Set the horizontal alignment in the box. */
    void setAlign(textAlign_t a)
    {
        align_ = a;
    }

    /** Set how text wider than the box is handled. */
    void setOverflow(Overflow o)
    {
        overflow_ = o;
    }

    /** The text's width by the font's line height. */
    Size natural() const override;

    /** Paint the text in the box. */
    void draw(DrawCtx &d) override;

private:
    UiText text_;                         ///< Text shown
    Sem color_ = Sem::OnSurface;          ///< Text colour role
    fontSize_t font_ = FONT_SIZE_8PT;     ///< Text font
    textAlign_t align_ = TEXT_ALIGN_LEFT; ///< Horizontal alignment
    Overflow overflow_ = Overflow::Clip;  ///< Handling of wide text
};

} // namespace ortxui

#endif /* ORTX_UI_LABEL_HPP */
