/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_TEXTBLOCK_HPP
#define ORTX_UI_TEXTBLOCK_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>
#include "base/Object.hpp"
#include "core/graphics.h"
#include "style/SemanticColor.hpp"

namespace ortxui
{

/**
 * Read-only wrapped text that scrolls vertically one line at a time, with
 * List's scroll indicator. The text is borrowed; the owner may re-point
 * it every frame (the message body moves inside the inbox pool), and the scroll
 * position is kept across re-points.
 */
class TextBlock : public Object
{
public:
    /** Constructor. */
    TextBlock() = default;

    /** Destructor. */
    ~TextBlock() = default;

    /** Point at new text, borrowed, keeping the scroll position. */
    void setText(const char *t)
    {
        text = (t != nullptr) ? t : "";
    }

    /** Set the text font. */
    void setFont(fontSize_t f)
    {
        textFont = f;
    }

    /** Set the text colour role. */
    void setColor(Sem c)
    {
        color = c;
    }

    /**
     * Scroll by whole lines.
     *
     * @param lines: lines to scroll, negative for up.
     * @return false if already at that end.
     */
    bool scroll(int lines);

    /** Show the first line. */
    void scrollToTop();

    /** Show the last line (resolved against the text at the next use). */
    void scrollToEnd();

    /**
     * Get the scroll offset as stored, to put it back with setScrollOffset().
     *
     * @return the offset in pixels.
     */
    int16_t scrollOffset() const
    {
        return scrollPos;
    }

    /**
     * Put back a scroll offset from scrollOffset().
     *
     * @param offset: offset in pixels.
     */
    void setScrollOffset(int16_t offset)
    {
        scrollPos = offset;
    }

    /** Draw the visible lines and, when the text overflows, the indicator. */
    void draw(DrawCtx &d) override;

private:
    /** Right edge (exclusive) the text wraps at, clear of the indicator. */
    int16_t wrapX() const;

    /** Height of the wrapped text in pixels. */
    uint16_t textHeight() const;

    /** Largest useful scroll offset for the current text and area. */
    int16_t maxOffset() const;

    /** Current scroll offset, clamped to the current text. */
    int16_t offset() const;

    const char *text = "";               ///< Borrowed text
    fontSize_t textFont = FONT_SIZE_8PT; ///< Text font
    Sem color = Sem::OnSurface;          ///< Text colour role
    int16_t scrollPos = 0;               ///< Scroll offset in pixels, clamped
};

} // namespace ortxui

#endif /* ORTX_UI_TEXTBLOCK_HPP */
