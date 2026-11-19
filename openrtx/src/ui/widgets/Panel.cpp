/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/Panel.hpp"
#include "render/DrawCtx.hpp"

namespace ortxui
{

void Panel::draw(DrawCtx &d)
{
    d.fillRect(area_, color_);
}

} // namespace ortxui
