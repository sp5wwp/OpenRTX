/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_TEXTROW_HPP
#define ORTX_UI_TEXTROW_HPP

#include <cstdint>
#include "core/Object.hpp"
#include "core/graphics.h"
#include "style/SemanticColor.hpp"

namespace ortxui
{

/**
 * A row of text "runs" — each with its own font and colour — that share a
 * single baseline. This is the engine-level answer to mixed-size text that must
 * bottom-align typographically (a large value next to a small unit/tag), which
 * the box-based Flex alignment cannot express; it replaces the hand-painted
 * baseline composites (TaggedValue, StatValue).
 *
 * Runs belong to a left group (packed left-to-right from the left inset) or a
 * right group (packed as a unit against the right inset, still drawn
 * left-to-right within the group). The shared baseline centres the tallest
 * run's line box in the row (CenterLine), so descenders and ascenders of every
 * run rest on the same line. Empty / hidden runs take no space.
 *
 * The backend anchors text by its baseline, so a run is drawn with one
 * DrawCtx::text() call at the computed baseline point — no per-widget metric
 * math at the call site.
 */
class TextRow : public Object
{
public:
    static constexpr uint8_t kMaxRuns = 4;

    enum class Side : uint8_t { Left, Right };

    /** Horizontal inset kept clear at both edges (0 = flush). */
    void setIndent(int16_t px)
    {
        indent_ = px;
    }
    /** Spacing inserted between adjacent runs of the same group. */
    void setGap(int16_t px)
    {
        gap_ = px;
    }

    /** Define run `i` (font / colour / group). Extends the run count to cover
     *  `i`. Text starts empty; set it with setText(). */
    void setRun(uint8_t i, fontSize_t font, Sem color, Side side);
    /** Update run `i`'s text (copied). An empty string hides the run. */
    void setText(uint8_t i, const char *text);
    /** Per-run colour override (e.g. a muted unit next to a bright value). */
    void setColor(uint8_t i, Sem color);

    Size natural() const override;
    void draw(DrawCtx &d) override;

private:
    struct Run {
        char text[16] = "";
        fontSize_t font = FONT_SIZE_8PT;
        Sem color = Sem::OnSurface;
        Side side = Side::Left;
        bool active = false; //< configured via setRun
    };

    bool visible(const Run &r) const
    {
        return r.active && (r.text[0] != '\0');
    }
    int16_t baselineY() const;

    Run runs_[kMaxRuns];
    uint8_t count_ = 0;
    int16_t indent_ = 0;
    int16_t gap_ = 3;
};

} // namespace ortxui

#endif /* ORTX_UI_TEXTROW_HPP */
