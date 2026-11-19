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

namespace
{

/* Top-of-tile labels (number key + icon / short label), one per key 1-9. */
const char *const kKeyLabels[9] = {
    "1 " SYMBOL_BELL,           // tone encode/decode
    "2 T-",                     // tone down
    "3 T+",                     // tone up
    "4 " SYMBOL_SIGNAL,         // bandwidth
    nullptr,                    // radio mode, see modeKey_
    "6 " SYMBOL_CHARGE,         // power
    "7 " SYMBOL_BRIGHTNESS "-", // brightness down
    "8 " SYMBOL_BRIGHTNESS "+", // brightness up
    "9 " SYMBOL_LOCK,           // keypad lock
};

/* Just the number keys — used on the tiny mono screen where the icons and a
 * second line per tile do not fit. */
const char *const kDigits[9] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

/* Tone encode/decode label, indexed by (txToneEn << 1) | rxToneEn. */
const UiText kEncDec[4] = { STR(None), STR(Decode), STR(Encode), STR(Both) };

int digitFromKeys(uint32_t keys)
{
    const uint32_t digits = keys & 0x3FFu;
    return (digits == 0u) ? -1 : __builtin_ctz(digits);
}

} // namespace

void MacroView::build()
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;
    const bool regular = (sizeClass() == SizeClass::Regular);
    const fontSize_t keyFont = regular ? FONT_SIZE_6PT : FONT_SIZE_5PT;
    const fontSize_t valFont = regular ? FONT_SIZE_8PT : FONT_SIZE_6PT;

    root_.setArea({ 0, 0, (uint16_t)W, (uint16_t)H });
    root_.setAxis(Axis::Column);

    /* Shared top bar (clock/battery/lock), same as every other screen. */
    TopBar &bar = useTopBar(STR(macroMenu));
    root_.addChild(&bar);

    /* Labels mixing a key or a glyph with a word, composed in the current
     * language. */
    snprintf(modeKey_, sizeof(modeKey_), "5 %s", STR(mode).str());
    snprintf(sqlLabel_, sizeof(sqlLabel_), SYMBOL_VOLUME " %s",
             STR(squelch).str());

    for (uint8_t r = 0; r < 3; r++) {
        rows_[r].setAxis(Axis::Row);
        rows_[r].setAlign(Align::Stretch);
        rows_[r].setGap(2);
        rows_[r].setGrow(1);

        for (uint8_t c = 0; c < 3; c++) {
            const uint8_t i = (uint8_t)(r * 3 + c);
            /* Regular: a 2-line tile (number + icon over the value). Compact
             * (mono): a single row "N value" — icons and a second line don't
             * fit on 128x64. */
            tiles_[i].setAxis(regular ? Axis::Column : Axis::Row);
            tiles_[i].setAlign(Align::Center);
            tiles_[i].setPadding(2, 1);
            tiles_[i].setGrow(1);

            keyLbl_[i].setFont(keyFont);
            keyLbl_[i].setAlign(regular ? TEXT_ALIGN_CENTER : TEXT_ALIGN_LEFT);
            keyLbl_[i].setColor(Sem::Accent);
            const char *key = (kKeyLabels[i] != nullptr) ? kKeyLabels[i] :
                                                           modeKey_;
            keyLbl_[i].setText(regular ? key : kDigits[i]);
            keyLbl_[i].setBasis(regular ? 12 : 9);

            valLbl_[i].setFont(valFont);
            valLbl_[i].setAlign(regular ? TEXT_ALIGN_CENTER : TEXT_ALIGN_RIGHT);
            valLbl_[i].setColor(Sem::OnSurface);
            valLbl_[i].setText(valBuf_[i]);
            valLbl_[i].setGrow(1);

            tiles_[i].addChild(&keyLbl_[i]);
            tiles_[i].addChild(&valLbl_[i]);
            rows_[r].addChild(&tiles_[i]);
        }
        root_.addChild(&rows_[r]);
    }

    footer_.setAxis(Axis::Row);
    footer_.setAlign(Align::Center);
    footer_.setBasis(regular ? 14 : 10);
    sqlBar_.setFont(regular ? FONT_SIZE_8PT : FONT_SIZE_6PT);
    sqlBar_.setLabel(sqlLabel_);
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
    screen_.markAllDirty();
}

void MacroView::composeValue(uint8_t tile, const state_t &s, char *out,
                             uint16_t n)
{
    const channel_t &ch = s.channel;
    const bool fm = (ch.mode == OPMODE_FM);

    switch (tile) {
        case 0: { /* tone encode/decode */
            const uint8_t enc = (uint8_t)((ch.fm.txToneEn << 1)
                                          | ch.fm.rxToneEn);
            snprintf(out, n, "%s", fm ? kEncDec[enc].str() : "-");
            break;
        }
        case 1: /* tone down (shows the tone) */
        case 2: /* tone up */
            if (fm) {
                const uint16_t t = ctcss_tone[ch.fm.txTone];
                snprintf(out, n, "%u.%u", (unsigned)(t / 10),
                         (unsigned)(t % 10));
            } else
                snprintf(out, n, "-");
            break;
        case 3: /* bandwidth */
            snprintf(out, n, "%s",
                     fm ? (ch.bandwidth == BW_25 ? "WFM" : "NFM") : "-");
            break;
        case 4: /* radio mode */
            snprintf(out, n, "%s",
                     (ch.mode == OPMODE_M17) ? "M17" : (fm ? "FM" : "DMR"));
            break;
        case 5: { /* power */
            const uint32_t mw = ch.power;
            if (mw >= 1000u)
                snprintf(out, n, "%lu.%luW", (unsigned long)(mw / 1000u),
                         (unsigned long)((mw % 1000u) / 100u));
            else
                snprintf(out, n, "%lumW", (unsigned long)mw);
            break;
        }
        case 6: /* brightness */
        case 7:
#ifdef CONFIG_SCREEN_BRIGHTNESS
            snprintf(out, n, "%u%%", (unsigned)s.settings.brightness);
#else
            snprintf(out, n, "-");
#endif
            break;
        case 8: /* keypad lock */
            snprintf(out, n, "%s",
                     (s.keypad_locked ? STR(on) : STR(off)).str());
            break;
        default:
            out[0] = '\0';
            break;
    }
}

void MacroView::syncFromState(const state_t &s)
{
    View::syncFromState(s);

    for (uint8_t i = 0; i < kTiles; i++) {
        char tmp[14];
        composeValue(i, s, tmp, sizeof(tmp));
        if (strncmp(tmp, valBuf_[i], sizeof(valBuf_[i])) != 0) {
            strncpy(valBuf_[i], tmp, sizeof(valBuf_[i]) - 1);
            valBuf_[i][sizeof(valBuf_[i]) - 1] = '\0';
            valLbl_[i].invalidate();
        }
    }

    /* Squelch is an FM-only RF gate (OpMode_FM); in the digital modes it does
     * nothing, so show it greyed and inert rather than letting it be set. */
    const bool fmSql = (s.channel.mode == OPMODE_FM);
    char sq[8];
    if (fmSql)
        snprintf(sq, sizeof(sq), "%u", (unsigned)s.settings.sqlLevel);
    else
        snprintf(sq, sizeof(sq), "--");
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

    if (e.kind == EvKind::Key) {
        const uint32_t k = e.keys;
        if ((k & (KEY_LEFT | KEY_DOWN)) != 0u)
            stepSquelch(-1);
        else if ((k & (KEY_RIGHT | KEY_UP)) != 0u)
            stepSquelch(+1);
        else {
            const int d = digitFromKeys(k);
            if (d >= 1 && d <= 9)
                doAction((uint8_t)d);
        }
    }
    return NavIntent::none();
}

} // namespace ortxui
