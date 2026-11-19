/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_BAR_HPP
#define ORTX_UI_BAR_HPP

#include "core/Object.hpp"
#include "style/SemanticColor.hpp"

namespace ortxui
{

/**
 * A horizontal level/progress bar. `value` is clamped to [0, 1].
 */
class Bar : public Object
{
public:
    void setValue(float v)
    {
        value_ = v;
    }
    void setColors(Sem track, Sem fill)
    {
        track_ = track;
        fill_ = fill;
    }

    void draw(DrawCtx &d) override;

private:
    float value_ = 0.0f;
    Sem track_ = Sem::SurfaceHigh;
    Sem fill_ = Sem::RxSuccess;
};

} // namespace ortxui

#endif /* ORTX_UI_BAR_HPP */
