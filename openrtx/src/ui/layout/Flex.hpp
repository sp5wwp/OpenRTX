/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_FLEX_HPP
#define ORTX_UI_FLEX_HPP

#include <cstdint>
#include "core/Object.hpp"
#include "core/Layout.hpp"
#include "style/SemanticColor.hpp"

namespace ortxui
{

/**
 * A flexbox-style layout container.
 *
 * Arranges its direct children along a main axis (Row/Column) inside its own
 * area: each child's main-axis size comes from its basis (or natural() size)
 * plus a share of the free space proportional to its grow weight, and free
 * space left over when nothing grows is distributed per the justify mode. On
 * the cross axis children are aligned (or stretched) per the container's
 * alignment and any per-child override. Children may carry a uniform margin.
 *
 * Layout is computed in onLayout(), which a parent calls after setting this
 * container's area; nested Flex children are recursed into, so a whole screen
 * lays out from one top-level call. An optional background colour lets a Flex
 * double as a styled surface (status/action bars) with no separate Panel.
 */
class Flex : public Object
{
public:
    void setAxis(Axis a)
    {
        axis_ = a;
    }
    void setJustify(Justify j)
    {
        justify_ = j;
    }
    /** Default cross-axis alignment for children (overridable per child). */
    void setAlign(Align a)
    {
        align_ = a;
    }
    void setGap(uint8_t g)
    {
        gap_ = g;
    }
    void setPadding(uint8_t p)
    {
        padX_ = p;
        padY_ = p;
    }
    void setPadding(uint8_t x, uint8_t y)
    {
        padX_ = x;
        padY_ = y;
    }
    void setBackground(Sem bg)
    {
        bg_ = bg;
        hasBg_ = true;
    }

    void draw(DrawCtx &d) override;
    void onLayout() override;

private:
    /** Preferred main-axis extent of a child before growth. */
    int16_t childBase(const Object &c) const;

    Axis axis_ = Axis::Row;
    Justify justify_ = Justify::Start;
    Align align_ = Align::Stretch;
    uint8_t gap_ = 0;
    uint8_t padX_ = 0;
    uint8_t padY_ = 0;
    Sem bg_ = Sem::Surface;
    bool hasBg_ = false;
};

} // namespace ortxui

#endif /* ORTX_UI_FLEX_HPP */
