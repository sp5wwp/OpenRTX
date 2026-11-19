/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/VolumeOverlayView.hpp"
#include "core/Event.hpp"

namespace ortxui
{

void VolumeOverlayView::build()
{
    screen_.markAllDirty();
}

void VolumeOverlayView::syncFromState(const state_t &s)
{
    /* No top bar on the HUD, so the base refresh is skipped; just track the
     * live knob level (change-gated so we only repaint when it moves). */
    if ((int16_t)s.volume == screen_.volume())
        return;

    screen_.setVolume((int16_t)s.volume);
    screen_.invalidate();
}

NavIntent VolumeOverlayView::onEvent(const Event &)
{
    /* The overlay never self-navigates: ui_shim opens it on knob movement and
     * closes it on the Transient timeout (or the next key press). */
    return NavIntent::none();
}

} // namespace ortxui
