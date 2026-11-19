/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/LowBatteryScreen.hpp"
#include "base/UiText.hpp"
#include "render/DrawCtx.hpp"
#include "widgets/BatteryIcon.hpp"
#include "hwconfig.h"

namespace ortxui
{

/** Charge the battery glyph shows. */
static constexpr uint8_t CHARGE_SHOWN = 10;

/** Baseline of the first prompt line. */
static constexpr int16_t PROMPT_Y = CONFIG_SCREEN_HEIGHT * 2 / 3;

/** Step from the first prompt line to the second. */
static constexpr int16_t PROMPT_STEP = 16;

/** Font of the prompt. */
static constexpr fontSize_t PROMPT_FONT = FONT_SIZE_6PT;

void LowBatteryScreen::draw(DrawCtx &d)
{
    const Rect battery = { CONFIG_SCREEN_WIDTH / 4, CONFIG_SCREEN_HEIGHT / 8,
                           CONFIG_SCREEN_WIDTH / 2, CONFIG_SCREEN_HEIGHT / 3 };
    BatteryIcon::paint(d, battery, CHARGE_SHOWN);

    d.text({ 0, PROMPT_Y }, PROMPT_FONT, TEXT_ALIGN_CENTER, Sem::OnSurface,
           STR(forEmergencyUse).str());
    d.text({ 0, static_cast<int16_t>(PROMPT_Y + PROMPT_STEP) }, PROMPT_FONT,
           TEXT_ALIGN_CENTER, Sem::OnSurface, STR(pressAnyButton).str());
}

} // namespace ortxui
