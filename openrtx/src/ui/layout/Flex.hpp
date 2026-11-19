/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_FLEX_HPP
#define ORTX_UI_FLEX_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

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
 * paint a styled surface such as a status bar.
 */
class Flex : public Object
{
public:
    /** Constructor. */
    Flex() = default;

    /** Destructor. */
    ~Flex() = default;

    /**
     * Set the main axis.
     *
     * @param a: axis.
     */
    void setAxis(Axis a)
    {
        axis_ = a;
    }

    /**
     * Set how free main-axis space is spread when no child grows.
     *
     * @param j: distribution.
     */
    void setJustify(Justify j)
    {
        justify_ = j;
    }

    /**
     * Set the default cross-axis alignment of the children.
     *
     * @param a: alignment, which a child can override.
     */
    void setAlign(Align a)
    {
        align_ = a;
    }

    /**
     * Set the gap between children.
     *
     * @param g: gap in pixels.
     */
    void setGap(uint8_t g)
    {
        gap_ = g;
    }

    /**
     * Set the same padding on every side.
     *
     * @param p: padding in pixels.
     */
    void setPadding(uint8_t p)
    {
        padX_ = p;
        padY_ = p;
    }

    /**
     * Set the padding.
     *
     * @param x: left and right padding in pixels.
     * @param y: top and bottom padding in pixels.
     */
    void setPadding(uint8_t x, uint8_t y)
    {
        padX_ = x;
        padY_ = y;
    }

    /**
     * Paint a background behind the children.
     *
     * @param bg: background colour role.
     */
    void setBackground(Sem bg)
    {
        bg_ = bg;
        hasBg_ = true;
    }

    /** Paint the background, if set. */
    void draw(DrawCtx &d) override;

    /** Place the children in the area, and lay each one out. */
    void onLayout() override;

private:
    /**
     * Get a child's main-axis extent before growth.
     *
     * @param c: child.
     * @param h: the child's layout hints.
     * @return the extent in pixels.
     */
    int16_t childBase(const Object &c, const LayoutHints &h) const;

    Axis axis_ = Axis::Row;            ///< Main axis
    Justify justify_ = Justify::Start; ///< Free space spread, nothing growing
    Align align_ = Align::Stretch;     ///< Default cross-axis alignment
    uint8_t gap_ = 0;                  ///< Gap between children
    uint8_t padX_ = 0;                 ///< Left and right padding
    uint8_t padY_ = 0;                 ///< Top and bottom padding
    Sem bg_ = Sem::Surface;            ///< Background colour role
    bool hasBg_ = false;               ///< The background is painted
};

} // namespace ortxui

#endif /* ORTX_UI_FLEX_HPP */
