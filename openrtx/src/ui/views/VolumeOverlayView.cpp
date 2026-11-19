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
    /* The HUD has no top bar, so the base refresh is not needed. */
    const int16_t volume = static_cast<int16_t>(s.volume);
    if (volume == screen_.volume())
        return;

    screen_.setVolume(volume);
    screen_.invalidate();
}

NavIntent VolumeOverlayView::onEvent(const Event &)
{
    /* The overlay never self-navigates: ui_shim opens it on knob movement and
     * closes it on the Transient timeout (or the next key press). */
    return NavIntent::none();
}

} // namespace ortxui
