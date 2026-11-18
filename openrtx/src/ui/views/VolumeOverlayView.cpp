/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/VolumeOverlayView.hpp"
#include "core/Event.hpp"
#include "core/graphics.h"
#include "hwconfig.h"

#include <cstdio>

namespace ortxui
{

void VolumeOverlayView::build()
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;
    const bool regular = (sizeClass() == SizeClass::Regular);

    root_.setArea({ 0, 0, (uint16_t)W, (uint16_t)H });
    root_.setAxis(Axis::Column);
    root_.setJustify(Justify::Center);
    root_.setAlign(Align::Stretch);
    root_.setPadding(6, 4);
    root_.setGap(regular ? 6 : 3);

    snprintf(titleBuf_, sizeof(titleBuf_), SYMBOL_VOLUME " %s",
             STR(volume).str());
    title_.setText(titleBuf_);
    title_.setFont(regular ? FONT_SIZE_12PT : FONT_SIZE_8PT);
    title_.setAlign(TEXT_ALIGN_CENTER);
    title_.setColor(Sem::Accent);
    title_.setBasis(regular ? 20 : 12);

    bar_.setFont(regular ? FONT_SIZE_8PT : FONT_SIZE_6PT);
    bar_.setColors(Sem::SurfaceHigh, Sem::Primary);
    bar_.setTextColor(Sem::OnSurface);
    bar_.setReadout(readoutBuf_);
    bar_.setBasis(regular ? 16 : 10);

    root_.addChild(&title_);
    root_.addChild(&bar_);

    screen_.addChild(&root_);
    root_.onLayout();
    screen_.markAllDirty();
}

void VolumeOverlayView::syncFromState(const state_t &s)
{
    /* No top bar on the HUD, so the base refresh is skipped; just track the
     * live knob level (change-gated so we only repaint when it moves). */
    if ((int16_t)s.volume == shownVol_)
        return;
    shownVol_ = (int16_t)s.volume;

    bar_.setValue(s.volume / 255.0f);
    const unsigned pct = (unsigned)((s.volume * 100u + 127u) / 255u);
    snprintf(readoutBuf_, sizeof(readoutBuf_), "%u%%", pct);
    bar_.invalidate();
}

NavIntent VolumeOverlayView::onEvent(const Event &)
{
    /* The overlay never self-navigates: ui_shim opens it on knob movement and
     * closes it on the Transient timeout (or the next key press). */
    return NavIntent::none();
}

} // namespace ortxui
