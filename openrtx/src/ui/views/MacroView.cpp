/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/MacroView.hpp"
#include "base/Event.hpp"
#include "views/UiMemory.hpp"
#include "base/UiFormat.hpp"
#include "core/cps.h"
#include "core/graphics.h"
#include "core/voicePromptUtils.h"
#include "style/Symbols.hpp"
#include "interfaces/keyboard.h"
#include "interfaces/display.h"
#include "hwconfig.h"

#include <cstdio>
#include <cstring>

namespace ortxui
{

/** Height of the squelch footer on the roomy screen. */
static constexpr int16_t FOOTER_H_REGULAR = 14;

/** Height of the squelch footer on the compact screen. */
static constexpr int16_t FOOTER_H_COMPACT = 10;

/** Highest squelch level. */
static constexpr uint8_t SQL_MAX = 15;

/** Lowest brightness the brightness actions set, in percent. */
static constexpr int BRIGHTNESS_MIN = 5;

/** Highest brightness the brightness actions set, in percent. */
static constexpr int BRIGHTNESS_MAX = 100;

/** Brightness step of the brightness actions, in percent. */
static constexpr int BRIGHTNESS_STEP = 5;

/**
 * Get the digit of a number key, ignoring the MONI bit.
 *
 * @param keys: keyboard bitmask.
 * @return the lowest digit pressed, -1 for none.
 */
static int digitFromKeys(uint32_t keys)
{
    const uint32_t digits = keys & KBD_NUM_MASK;
    return (digits == 0u) ? -1 : __builtin_ctz(digits);
}

/**
 * Speak the CTCSS settings of the current channel.
 *
 * @param flags: voice prompt queue flags.
 */
static void announceTones(enum vpQueueFlags flags)
{
    const channel_t &ch = state.channel;
    vp_announceCTCSS(ch.fm.rxToneEn, ch.fm.rxTone, ch.fm.txToneEn, ch.fm.txTone,
                     flags);
}

/**
 * Check whether a macro action steps a value, so that it repeats while held.
 *
 * @param action: action.
 * @return true for the tone and brightness steps.
 */
static bool isStepAction(MacroAction action)
{
    switch (action) {
        case MacroAction::ToneDown:
        case MacroAction::ToneUp:
        case MacroAction::BrightnessDown:
        case MacroAction::BrightnessUp:
            return true;
        default:
            return false;
    }
}

void MacroView::build()
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;
    const bool regular = (sizeClass() == SizeClass::Regular);

    root.setArea({ 0, 0, static_cast<uint16_t>(W), static_cast<uint16_t>(H) });
    root.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(macroMenu));
    root.addChild(&bar);

    tiles.setGrow(1);
    root.addChild(&tiles);

    footer.setAxis(Axis::Row);
    footer.setAlign(Align::Center);
    footer.setBasis(regular ? FOOTER_H_REGULAR : FOOTER_H_COMPACT);
    sqlBar.setFont(regular ? FONT_SIZE_8PT : FONT_SIZE_6PT);
    sqlBar.setLabel(STR(squelch), SYMBOL_VOLUME);
    /* Red, like the squelch mark on the VFO meter. */
    sqlBar.setColors(Sem::SurfaceHigh, Sem::TxDanger);
    sqlBar.setTextColor(Sem::OnSurface);
    sqlBar.setReadout(sqlBuf);
    sqlBar.setGrow(1);
    footer.addChild(&sqlBar);
    root.addChild(&footer);

    tree.addChild(&root);
    root.onLayout();
    tree.markAllDirty();
}

void MacroView::save() const
{
    uiMemory.macroTile = selectedTile;
}

void MacroView::restore()
{
    selectedTile = uiMemory.macroTile;
#if defined(CONFIG_UI_NO_KEYBOARD)
    tiles.select(selectedTile);
#endif
}

void MacroView::syncFromState(const state_t &s)
{
    View::syncFromState(s);

    if (tiles.sync(s, uiMemory.keypadLocked))
        tiles.invalidate();

    /* Squelch gates only FM: grey it out in the other modes. */
    const bool fmSql = (s.channel.mode == OPMODE_FM);
    char sq[8];
    if (fmSql)
        sniprintf(sq, sizeof(sq), "%u",
                  static_cast<unsigned>(s.settings.sqlLevel));
    else
        sniprintf(sq, sizeof(sq), "--");
    if (strncmp(sq, sqlBuf, sizeof(sqlBuf)) != 0) {
        strncpy(sqlBuf, sq, sizeof(sqlBuf) - 1);
        sqlBuf[sizeof(sqlBuf) - 1] = '\0';
        sqlBar.setValue(
            fmSql ? s.settings.sqlLevel / static_cast<float>(SQL_MAX) : 0.0f);
        sqlBar.setColors(Sem::SurfaceHigh,
                         fmSql ? Sem::TxDanger : Sem::OnSurfaceMuted);
        sqlBar.setTextColor(fmSql ? Sem::OnSurface : Sem::OnSurfaceMuted);
        sqlBar.invalidate();
    }
}

void MacroView::stepSquelch(int dir)
{
    /* Squelch gates only FM. */
    if (state.channel.mode != OPMODE_FM)
        return;

    int v = static_cast<int>(state.settings.sqlLevel) + dir;
    if (v < 0)
        v = 0;
    if (v > SQL_MAX)
        v = SQL_MAX;
    if (static_cast<uint8_t>(v) != state.settings.sqlLevel) {
        state.settings.sqlLevel = static_cast<uint8_t>(v);
        requestSyncRtx();
        vp_announceSquelch(state.settings.sqlLevel,
                           vp_getVoiceLevelQueueFlags());
    }
}

void MacroView::doAction(MacroAction action)
{
    channel_t &ch = state.channel;
    const bool fm = (ch.mode == OPMODE_FM);
    const enum vpQueueFlags flags = vp_getVoiceLevelQueueFlags();

    switch (action) {
        case MacroAction::ToneEnable:
            if (fm) {
                stepToneEnable(ch.fm, +1);
                requestSyncRtx();
                announceTones(
                    static_cast<vpQueueFlags>(flags | vpqIncludeDescriptions));
            }
            break;
        case MacroAction::ToneDown:
            if (fm) {
                stepTone(ch.fm, -1);
                requestSyncRtx();
                announceTones(flags);
            }
            break;
        case MacroAction::ToneUp:
            if (fm) {
                stepTone(ch.fm, +1);
                requestSyncRtx();
                announceTones(
                    static_cast<vpQueueFlags>(flags | vpqIncludeDescriptions));
            }
            break;
        case MacroAction::Bandwidth:
            if (fm) {
                ch.bandwidth = static_cast<uint8_t>((ch.bandwidth + 1) % 2);
                requestSyncRtx();
                vp_announceBandwidth(ch.bandwidth, flags);
            }
            break;
        case MacroAction::Mode:
#ifdef CONFIG_M17
            ch.mode = (ch.mode == OPMODE_FM) ? OPMODE_M17 : OPMODE_FM;
#else
            ch.mode = OPMODE_FM;
#endif
            requestSyncRtx();
            vp_announceRadioMode(ch.mode, flags);
            break;
        case MacroAction::Power:
            ch.power = nextPower(ch.power);
            requestSyncRtx();
            vp_announcePower(ch.power, flags);
            break;
#ifdef CONFIG_SCREEN_BRIGHTNESS
        case MacroAction::BrightnessDown:
        case MacroAction::BrightnessUp: {
            const int step = (action == MacroAction::BrightnessUp) ?
                                 BRIGHTNESS_STEP :
                                 -BRIGHTNESS_STEP;
            int b = static_cast<int>(state.settings.brightness) + step;
            if (b < BRIGHTNESS_MIN)
                b = BRIGHTNESS_MIN;
            if (b > BRIGHTNESS_MAX)
                b = BRIGHTNESS_MAX;
            state.settings.brightness = static_cast<uint8_t>(b);
            display_setBacklightLevel(static_cast<uint8_t>(b));
            vp_announceSettingsInt(&currentLanguage->brightness, flags, b);
            break;
        }
#endif
        case MacroAction::KeypadLock:
            uiMemory.keypadLocked = !uiMemory.keypadLocked;
            break;
        default:
            break;
    }
}

void MacroView::onKeypadEvent(const Event &e)
{
    /* Number keys arrive with the MONI bit also set, which digitFromKeys()
     * ignores. */
    if (e.kind == EvKind::Encoder) {
        stepSquelch(e.encoder);
        return;
    }

    /* Held keys keep stepping the squelch, the tone and the brightness; the
     * toggles and cycles act once. */
    const bool repeat = (e.kind == EvKind::KeyRepeat);
    if ((e.kind != EvKind::Key) && !repeat)
        return;

    const uint32_t k = e.keys;
    if ((k & (KEY_LEFT | KEY_DOWN)) != 0u) {
        stepSquelch(-1);
    } else if ((k & (KEY_RIGHT | KEY_UP)) != 0u) {
        stepSquelch(+1);
    } else {
        const int d = digitFromKeys(k);
        if ((d >= 1) && (d <= MacroTiles::TILES)) {
            const MacroAction action = static_cast<MacroAction>(d - 1);
            if (!repeat || isStepAction(action))
                doAction(action);
        }
    }
}

void MacroView::onKnobEvent(const Event &e)
{
    if (e.kind == EvKind::Encoder) {
        selectedTile = static_cast<uint8_t>(
            (selectedTile + MacroTiles::TILES + e.encoder) % MacroTiles::TILES);
        tiles.select(selectedTile);
        tiles.invalidate();
    } else if (e.kind == EvKind::Key) {
        if ((e.keys & KEY_ENTER) != 0u)
            doAction(static_cast<MacroAction>(selectedTile));
        else if ((e.keys & KEY_VOLDOWN) != 0u)
            stepSquelch(-1);
        else if ((e.keys & KEY_VOLUP) != 0u)
            stepSquelch(+1);
    }
}

NavIntent MacroView::onEvent(const Event &e)
{
    /* The menu never navigates: ui_shim opens and closes it. */
#if defined(CONFIG_UI_NO_KEYBOARD)
    onKnobEvent(e);
#else
    onKeypadEvent(e);
#endif
    return NavIntent::none();
}

} // namespace ortxui
