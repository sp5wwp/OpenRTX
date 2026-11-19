/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/MacroView.hpp"
#include "core/Event.hpp"
#include "core/cps.h"
#include "core/graphics.h"
#include "interfaces/keyboard.h"
#include "interfaces/display.h"
#include "hwconfig.h"

#include <cstdio>
#include <cstring>

namespace ortxui
{

/**
 * Get the digit of a number key, ignoring the MONI bit.
 *
 * @param keys: keyboard bitmask.
 * @return the lowest digit pressed, -1 for none.
 */
static int digitFromKeys(uint32_t keys)
{
    const uint32_t digits = keys & 0x3FFu;
    return (digits == 0u) ? -1 : __builtin_ctz(digits);
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

    root_.setArea({ 0, 0, (uint16_t)W, (uint16_t)H });
    root_.setAxis(Axis::Column);

    /* Shared top bar (clock/battery/lock), same as every other screen. */
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
    sqlBar_.setColors(Sem::SurfaceHigh, Sem::TxDanger); /* squelch = red, as on
                                                           the VFO meter gate */
    sqlBar_.setTextColor(Sem::OnSurface);
    sqlBar_.setReadout(sqlBuf_);
    /* Fill the footer width via grow (BarRow's natural() exceeds the screen). */
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
        sqlBar_.setValue(fmSql ? s.settings.sqlLevel / 15.0f : 0.0f);
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

    int v = (int)state.settings.sqlLevel + dir;
    if (v < 0)
        v = 0;
    if (v > 15)
        v = 15;
    if ((uint8_t)v != state.settings.sqlLevel) {
        state.settings.sqlLevel = (uint8_t)v;
        requestSyncRtx();
    }
}

void MacroView::doAction(uint8_t number)
{
    channel_t &ch = state.channel;
    const bool fm = (ch.mode == OPMODE_FM);

    switch (number) {
        case 1: /* tone encode/decode cycle (FM) */
            if (fm) {
                uint8_t f = (uint8_t)((ch.fm.txToneEn << 1) | ch.fm.rxToneEn);
                f = (uint8_t)((f + 1) & 3);
                ch.fm.txToneEn = (uint8_t)((f >> 1) & 1);
                ch.fm.rxToneEn = (uint8_t)(f & 1);
                requestSyncRtx();
            }
            break;
        case 2: /* tone down (FM) */
            if (fm) {
                ch.fm.txTone = (uint8_t)((ch.fm.txTone + CTCSS_FREQ_NUM - 1)
                                         % CTCSS_FREQ_NUM);
                ch.fm.rxTone = ch.fm.txTone;
                requestSyncRtx();
            }
            break;
        case 3: /* tone up (FM) */
            if (fm) {
                ch.fm.txTone = (uint8_t)((ch.fm.txTone + 1) % CTCSS_FREQ_NUM);
                ch.fm.rxTone = ch.fm.txTone;
                requestSyncRtx();
            }
            break;
        case 4: /* bandwidth (FM) */
            if (fm) {
                ch.bandwidth = (uint8_t)((ch.bandwidth + 1) % 2);
                requestSyncRtx();
            }
            break;
        case 5: /* radio mode */
#ifdef CONFIG_M17
            ch.mode = (ch.mode == OPMODE_FM) ? OPMODE_M17 : OPMODE_FM;
#else
            ch.mode = OPMODE_FM;
#endif
            requestSyncRtx();
            break;
        case 6: /* power cycle 1 / 2.5 / 5 W */
            ch.power = (ch.power == 1000u) ? 2500u :
                       (ch.power == 2500u) ? 5000u :
                                             1000u;
            requestSyncRtx();
            break;
#ifdef CONFIG_SCREEN_BRIGHTNESS
        case 7:   /* brightness down */
        case 8: { /* brightness up */
            int b = (int)state.settings.brightness + (number == 8 ? 5 : -5);
            if (b < 5)
                b = 5;
            if (b > 100)
                b = 100;
            state.settings.brightness = (uint8_t)b;
            display_setBacklightLevel((uint8_t)b);
            break;
        }
#endif
        case 9: /* keypad lock toggle */
            state.keypad_locked = !state.keypad_locked;
            break;
        default:
            break;
    }
}

NavIntent MacroView::onEvent(const Event &e)
{
    /* The macro overlay never self-navigates — ui_shim opens/closes it from the
     * MONI key. It just applies quick actions from the number/arrow keys (which
     * arrive with the MONI bit also set, ignored by digitFromKeys). */
    if (e.kind == EvKind::Encoder) {
        stepSquelch(e.encoder);
        return NavIntent::none();
    }

    /* Held keys keep stepping the squelch, the tone and the brightness; the
     * toggles and cycles act once. */
    const bool repeat = (e.kind == EvKind::KeyRepeat);
    if ((e.kind == EvKind::Key) || repeat) {
        const uint32_t k = e.keys;
        if ((k & (KEY_LEFT | KEY_DOWN)) != 0u)
            stepSquelch(-1);
        else if ((k & (KEY_RIGHT | KEY_UP)) != 0u)
            stepSquelch(+1);
        else {
            const int d = digitFromKeys(k);
            const bool acts = (d >= 1) && (d <= 9);
            if (acts && (!repeat || isStepAction((uint8_t)d)))
                doAction((uint8_t)d);
        }
    }
    return NavIntent::none();
}

} // namespace ortxui
