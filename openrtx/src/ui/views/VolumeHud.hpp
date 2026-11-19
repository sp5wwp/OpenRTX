/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_VOLUMEHUD_HPP
#define ORTX_UI_VOLUMEHUD_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstdint>
#include "base/Screen.hpp"

namespace ortxui
{

/**
 * The volume HUD's screen: a padded column centring the title over the level
 * bar, painted from the level shown.
 */
class VolumeHud : public Screen
{
public:
    /** Constructor. */
    VolumeHud() = default;

    /** Destructor. */
    ~VolumeHud() = default;

    /** Paint the title and the level bar. */
    void draw(DrawCtx &d) override;

    /**
     * Get the level shown.
     *
     * @return the level, 0 to 255, or -1 before the first paint.
     */
    int16_t volume() const
    {
        return volume_;
    }

    /**
     * Set the level shown.
     *
     * @param v: level, 0 to 255.
     */
    void setVolume(int16_t v)
    {
        volume_ = v;
    }

private:
    int16_t volume_ = -1; ///< Level shown, -1 forces the first paint
};

} // namespace ortxui

#endif /* ORTX_UI_VOLUMEHUD_HPP */
