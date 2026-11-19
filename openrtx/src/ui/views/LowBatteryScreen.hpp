/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_LOWBATTERYSCREEN_HPP
#define ORTX_UI_LOWBATTERYSCREEN_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include "base/Screen.hpp"

namespace ortxui
{

/**
 * The low-battery warning: a nearly empty battery over the prompt to press
 * any button for emergency use.
 */
class LowBatteryScreen : public Screen
{
public:
    /** Constructor. */
    LowBatteryScreen() = default;

    /** Destructor. */
    ~LowBatteryScreen() = default;

    /** Paint the battery and the prompt. */
    void draw(DrawCtx &d) override;
};

} // namespace ortxui

#endif /* ORTX_UI_LOWBATTERYSCREEN_HPP */
