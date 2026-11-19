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

    /**
     * How text that is wider than the label's box is handled:
     *   Wrap      - the backend wraps to further lines (the historic default).
     *   Ellipsize - the text is kept to one line, truncated to fit with a
     *               trailing ellipsis glyph (…).
     */
    enum class Overflow : uint8_t {
        Wrap,
        Ellipsize,
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
    void setColor(Sem c)
    {
        color_ = c;
    }
    void setFont(fontSize_t f)
    {
        font_ = f;
    }

    /** Font the text is drawn in. */
    fontSize_t font() const
    {
        return font_;
    }
    void setAlign(textAlign_t a)
    {
        align_ = a;
    }
    void setOverflow(Overflow o)
    {
        overflow_ = o;
    }

    Size natural() const override; //< measured text width x font height
    void draw(DrawCtx &d) override;

private:
    UiText text_;
    Sem color_ = Sem::OnSurface;
    fontSize_t font_ = FONT_SIZE_8PT;
    textAlign_t align_ = TEXT_ALIGN_LEFT;
    Overflow overflow_ = Overflow::Wrap;
};

} // namespace ortxui

#endif /* ORTX_UI_LABEL_HPP */
