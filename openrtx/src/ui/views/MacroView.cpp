/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/MacroView.hpp"
#include "core/Event.hpp"
#include "core/UiMemory.hpp"
#include "core/UiFormat.hpp"
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

/* Highest squelch level. */
static constexpr uint8_t SQL_MAX = 15;

/* Brightness range and step of the brightness actions, in percent. */
static constexpr int BRIGHTNESS_MIN = 5;
static constexpr int BRIGHTNESS_MAX = 100;
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
 * @param number: number key of the action.
 * @return true for the tone and brightness steps.
 */
static bool isStepAction(uint8_t number)
{
    return (number == 2) || (number == 3) || (number == 7) || (number == 8);
}

void MacroView::build()
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;
    const bool regular = (sizeClass() == SizeClass::Regular);

    root_.setArea({ 0, 0, static_cast<uint16_t>(W), static_cast<uint16_t>(H) });
    root_.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(macroMenu));
    root_.addChild(&bar);

    /* The grid rows share the free height; the tiles are placed over them. */
    for (uint8_t r = 0; r < 3; r++) {
        rows_[r].setGrow(1);
        root_.addChild(&rows_[r]);
    }

    footer_.setAxis(Axis::Row);
    footer_.setAlign(Align::Center);
    footer_.setBasis(regular ? 14 : 10);
    sqlBar_.setFont(regular ? FONT_SIZE_8PT : FONT_SIZE_6PT);
    sqlBar_.setLabel(STR(squelch), SYMBOL_VOLUME);
    /* Red, like the squelch mark on the VFO meter. */
    sqlBar_.setColors(Sem::SurfaceHigh, Sem::TxDanger);
    sqlBar_.setTextColor(Sem::OnSurface);
    sqlBar_.setReadout(sqlBuf_);
    /* Fill the footer width via grow. */
    sqlBar_.setBasis(0);
    sqlBar_.setGrow(1);
    footer_.addChild(&sqlBar_);
    root_.addChild(&footer_);

    screen_.addChild(&root_);
    root_.onLayout();

    /* The tiles cover the three rows. */
    const Rect &top = rows_[0].area();
    tiles_.setArea(
        { top.x, top.y, top.w,
          static_cast<uint16_t>(rows_[2].area().bottom() - top.y + 1) });
    screen_.addChild(&tiles_);
#if defined(CONFIG_UI_NO_KEYBOARD)
    tiles_.select(selected_);
#endif
    screen_.markAllDirty();
}

void MacroView::syncFromState(const state_t &s)
{
    View::syncFromState(s);

    if (tiles_.sync(s))
        tiles_.invalidate();

    /* Squelch is an FM-only RF gate (OpMode_FM); in the digital modes it does
     * nothing, so show it greyed and inert rather than letting it be set. */
    const bool fmSql = (s.channel.mode == OPMODE_FM);
    char sq[8];
    if (fmSql)
        sniprintf(sq, sizeof(sq), "%u", (unsigned)s.settings.sqlLevel);
    else
        sniprintf(sq, sizeof(sq), "--");
    if (strncmp(sq, sqlBuf_, sizeof(sqlBuf_)) != 0) {
        strncpy(sqlBuf_, sq, sizeof(sqlBuf_) - 1);
        sqlBuf_[sizeof(sqlBuf_) - 1] = '\0';
        sqlBar_.setValue(
            fmSql ? s.settings.sqlLevel / static_cast<float>(SQL_MAX) : 0.0f);
        sqlBar_.setColors(Sem::SurfaceHigh,
                          fmSql ? Sem::TxDanger : Sem::OnSurfaceMuted);
        sqlBar_.setTextColor(fmSql ? Sem::OnSurface : Sem::OnSurfaceMuted);
        sqlBar_.invalidate();
    }
}

void MacroView::stepSquelch(int dir)
{
    /* No-op outside FM: squelch only gates the analogue RX path. */
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

void MacroView::doAction(uint8_t number)
{
    channel_t &ch = state.channel;
    const bool fm = (ch.mode == OPMODE_FM);
    const enum vpQueueFlags flags = vp_getVoiceLevelQueueFlags();

    switch (number) {
        case 1: /* tone encode/decode cycle (FM) */
            if (fm) {
                stepToneEnable(ch.fm, +1);
                requestSyncRtx();
                announceTones(
                    static_cast<vpQueueFlags>(flags | vpqIncludeDescriptions));
            }
            break;
        case 2: /* tone down (FM) */
            if (fm) {
                stepTone(ch.fm, -1);
                requestSyncRtx();
                announceTones(flags);
            }
            break;
        case 3: /* tone up (FM) */
            if (fm) {
                stepTone(ch.fm, +1);
                requestSyncRtx();
                announceTones(
                    static_cast<vpQueueFlags>(flags | vpqIncludeDescriptions));
            }
            break;
        case 4: /* bandwidth (FM) */
            if (fm) {
                ch.bandwidth = (uint8_t)((ch.bandwidth + 1) % 2);
                requestSyncRtx();
                vp_announceBandwidth(ch.bandwidth, flags);
            }
            break;
        case 5: /* radio mode */
#ifdef CONFIG_M17
            ch.mode = (ch.mode == OPMODE_FM) ? OPMODE_M17 : OPMODE_FM;
#else
            ch.mode = OPMODE_FM;
#endif
            requestSyncRtx();
            vp_announceRadioMode(ch.mode, flags);
            break;
        case 6: /* power cycle 1 / 2.5 / 5 W */
            ch.power = (ch.power == 1000u) ? 2500u :
                       (ch.power == 2500u) ? 5000u :
                                             1000u;
            requestSyncRtx();
            vp_announcePower(ch.power, flags);
            break;
#ifdef CONFIG_SCREEN_BRIGHTNESS
        case 7:   /* brightness down */
        case 8: { /* brightness up */
            const int step = (number == 8) ? BRIGHTNESS_STEP : -BRIGHTNESS_STEP;
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
        case 9: /* keypad lock toggle */
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
        const bool acts = (d >= 1) && (d <= 9);
        if (acts && (!repeat || isStepAction((uint8_t)d)))
            doAction((uint8_t)d);
    }
}

void MacroView::onKnobEvent(const Event &e)
{
    if (e.kind == EvKind::Encoder) {
        selected_ = static_cast<uint8_t>(
            (selected_ + MacroTiles::TILES + e.encoder) % MacroTiles::TILES);
        tiles_.select(selected_);
        tiles_.invalidate();
    } else if (e.kind == EvKind::Key) {
        if ((e.keys & KEY_ENTER) != 0u)
            doAction(static_cast<uint8_t>(selected_ + 1));
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
