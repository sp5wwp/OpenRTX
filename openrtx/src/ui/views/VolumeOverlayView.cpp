/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/VolumeOverlayView.hpp"

namespace ortxui
{

void VolumeOverlayView::build()
{
    tree.markAllDirty();
}

void VolumeOverlayView::syncFromState(const state_t &s)
{
    /* The HUD has no top bar, so the base refresh is not needed. */
    const int16_t volume = static_cast<int16_t>(s.volume);
    if (volume == tree.volume())
        return;

    tree.setVolume(volume);
    tree.invalidate();
}

} // namespace ortxui
