/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_PANEL_HPP
#define ORTX_UI_PANEL_HPP

#include "core/Object.hpp"
#include "style/SemanticColor.hpp"

namespace ortxui
{

/**
 * A solid filled rectangle in a role colour (status/action bars, surfaces).
 */
class Panel : public Object
{
public:
    void setColor(Sem c)
    {
        color_ = c;
    }

    void draw(DrawCtx &d) override;

private:
    Sem color_ = Sem::Surface;
};

} // namespace ortxui

#endif /* ORTX_UI_PANEL_HPP */
