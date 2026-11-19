/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_TEXTROW_HPP
#define ORTX_UI_TEXTROW_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <array>
#include <cstdint>
#include "core/Object.hpp"
#include "core/graphics.h"
#include "style/SemanticColor.hpp"

namespace ortxui
{

/**
 * Text runs of different fonts on one shared baseline, packed against the
 * right edge, such as a value next to its smaller unit: box alignment cannot
 * line up text of mixed sizes. The baseline centres the tallest run's line in
 * the row; a run with no text takes no space.
 */
class TextRow : public Object
{
public:
    /** Constructor. */
    TextRow() = default;

    /** Destructor. */
    ~TextRow() = default;

    /** Runs a row holds: a value and its tag or unit. */
    static constexpr uint8_t MAX_RUNS = 2;

    /**
     * Set the space between runs.
     *
     * @param px: space in pixels.
     */
    void setGap(int16_t px)
    {
        gap_ = px;
    }

    /**
     * Define a run, with no text yet.
     *
     * @param i: run index, below MAX_RUNS.
     * @param font: font.
     * @param color: colour role.
     */
    void setRun(uint8_t i, fontSize_t font, Sem color);

    /**
     * Update a run's text. The text is borrowed, not copied, so the caller
     * keeps it alive while the row may draw.
     *
     * @param i: run index.
     * @param text: text; empty or nullptr hides the run.
     */
    void setText(uint8_t i, const char *text);

    /**
     * Get the size of the row: the tallest run's line, whatever the text, and
     * the width of the text set.
     *
     * @return the size.
     */
    Size natural() const override;

    /**
     * Paint the runs.
     *
     * @param d: drawing context.
     */
    void draw(DrawCtx &d) override;

private:
    /**
     * One run of text.
     */
    struct Run {
        const char *text = "";           ///< Borrowed text
        fontSize_t font = FONT_SIZE_8PT; ///< Font
        Sem color = Sem::OnSurface;      ///< Colour role
    };

    /**
     * Check whether a run takes space.
     *
     * @param r: run.
     * @return true if it has text.
     */
    static bool visible(const Run &r)
    {
        return r.text[0] != '\0';
    }

    /**
     * Get the width of the runs with text, with the gaps between them.
     *
     * @return the width in pixels.
     */
    int16_t textWidth() const;

    /** Get the shared baseline, from the fonts and not the text. */
    int16_t baselineY() const;

    std::array<Run, MAX_RUNS> runs_; ///< The runs, left to right
    uint8_t count_ = 0;              ///< Runs defined
    int16_t gap_ = 3;                ///< Space between runs
};

} // namespace ortxui

#endif /* ORTX_UI_TEXTROW_HPP */
