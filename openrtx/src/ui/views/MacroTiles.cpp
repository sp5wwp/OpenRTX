/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/MacroTiles.hpp"
#include "core/UiText.hpp"
#include "core/cps.h"
#include "core/graphics.h"
#include "layout/Flex.hpp"
#include "render/DrawCtx.hpp"
#include "style/Symbols.hpp"
#include "widgets/Scratch.hpp"
#include "widgets/Widgets.hpp"
#include "hwconfig.h"

#include <cstdio>

namespace ortxui
{

/* Top-of-tile labels (number key + icon / short label), one per key 1-9. */
static const char *const KEY_LABELS[MacroTiles::TILES] = {
    "1 " SYMBOL_BELL,           // tone encode/decode
    "2 T-",                     // tone down
    "3 T+",                     // tone up
    "4 " SYMBOL_SIGNAL,         // bandwidth
    nullptr,                    // radio mode, "5 " + the mode label
    "6 " SYMBOL_CHARGE,         // power
    "7 " SYMBOL_BRIGHTNESS "-", // brightness down
    "8 " SYMBOL_BRIGHTNESS "+", // brightness up
    "9 " SYMBOL_LOCK,           // keypad lock
};

/* Just the number keys — used on the tiny mono screen where the icons and a
 * second line per tile do not fit. */
static const char *const DIGITS[MacroTiles::TILES] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9",
};

/* Tone encode/decode label, indexed by (txToneEn << 1) | rxToneEn. */
static const UiText ENC_DEC[4] = {
    STR(None),
    STR(Decode),
    STR(Encode),
    STR(Both),
};

/* Gap between the tiles of a row. */
static constexpr uint8_t TILE_GAP = 2;

/* Size of a composed tile value. */
static constexpr uint16_t VALUE_SIZE = 14;

/* Size of a composed key label. */
static constexpr uint16_t KEY_SIZE = 16;

/* FNV-1a offset basis and prime, for the value digest. */
static constexpr uint32_t FNV_BASIS = 2166136261u;
static constexpr uint32_t FNV_PRIME = 16777619u;

/**
 * One tile: a key label over (or, on the compact screen, beside) the value.
 * Every tile is laid out and painted through this one, in the scratch.
 */
struct ScratchTile {
    /** Constructor. */
    ScratchTile()
    {
        box.addChild(&key);
        box.addChild(&value);
    }

    Flex box;    ///< Tile container
    Label key;   ///< Number key and icon
    Label value; ///< Live value
};

MacroTiles::MacroTiles() : tile_(), key_(), value_()
{
}

MacroTiles::~MacroTiles()
{
}

void MacroTiles::setUp(uint8_t tile, char *keyBuf)
{
    const bool regular = (sizeClass() == SizeClass::Regular);
    ScratchTile &scratch = Scratch::get<ScratchTile>();

    /* Regular: a 2-line tile (number + icon over the value). Compact (mono):
     * a single row "N value" — icons and a second line don't fit on 128x64. */
    scratch.box.setAxis(regular ? Axis::Column : Axis::Row);
    scratch.box.setAlign(Align::Center);
    scratch.box.setPadding(2, 1);

    if (KEY_LABELS[tile] != nullptr)
        snprintf(keyBuf, KEY_SIZE, "%s", KEY_LABELS[tile]);
    else
        snprintf(keyBuf, KEY_SIZE, "5 %s", STR(mode).str());

    scratch.key.setFont(regular ? FONT_SIZE_6PT : FONT_SIZE_5PT);
    scratch.key.setAlign(regular ? TEXT_ALIGN_CENTER : TEXT_ALIGN_LEFT);
    scratch.key.setColor(Sem::Accent);
    scratch.key.setText(regular ? keyBuf : DIGITS[tile]);
    scratch.key.setBasis(regular ? 12 : 9);

    scratch.value.setFont(regular ? FONT_SIZE_8PT : FONT_SIZE_6PT);
    scratch.value.setAlign(regular ? TEXT_ALIGN_CENTER : TEXT_ALIGN_RIGHT);
    scratch.value.setColor(Sem::OnSurface);
    scratch.value.setText("");
    scratch.value.setGrow(1);
}

void MacroTiles::place(const Rect *rows)
{
    char keyBuf[KEY_SIZE];

    for (uint8_t r = 0; r < 3; r++) {
        /* A row of three tiles growing equally, TILE_GAP apart. */
        const Rect &row = rows[r];
        int free = static_cast<int>(row.w) - 2 * TILE_GAP;
        if (free < 0)
            free = 0;
        const uint16_t w = static_cast<uint16_t>(free / 3);

        for (uint8_t c = 0; c < 3; c++) {
            const uint8_t i = static_cast<uint8_t>(r * 3 + c);
            if ((row.w == 0) || (row.h == 0)) {
                tile_[i] = { 0, 0, 0, 0 };
                key_[i] = { 0, 0, 0, 0 };
                value_[i] = { 0, 0, 0, 0 };
                continue;
            }

            tile_[i] = { static_cast<int16_t>(row.x + c * (w + TILE_GAP)),
                         row.y, w, row.h };

            /* The labels are laid out with an empty value, as when built. */
            setUp(i, keyBuf);
            ScratchTile &scratch = Scratch::get<ScratchTile>();
            scratch.key.setArea({ 0, 0, 0, 0 });
            scratch.value.setArea({ 0, 0, 0, 0 });
            scratch.box.setArea(tile_[i]);
            scratch.box.onLayout();
            key_[i] = scratch.key.area();
            value_[i] = scratch.value.area();
        }
    }
}

void MacroTiles::composeValue(uint8_t tile, const state_t &s, char *out,
                              uint16_t n)
{
    const channel_t &ch = s.channel;
    const bool fm = (ch.mode == OPMODE_FM);

    switch (tile) {
        case 0: { /* tone encode/decode */
            const uint8_t enc = (uint8_t)((ch.fm.txToneEn << 1)
                                          | ch.fm.rxToneEn);
            snprintf(out, n, "%s", fm ? ENC_DEC[enc].str() : "-");
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

bool MacroTiles::sync(const state_t &s)
{
    state_ = &s;

    uint32_t h = FNV_BASIS;
    for (uint8_t i = 0; i < TILES; i++) {
        char value[VALUE_SIZE];
        composeValue(i, s, value, sizeof(value));
        for (const char *c = value; *c != '\0'; c++)
            h = (h ^ static_cast<uint8_t>(*c)) * FNV_PRIME;
        h *= FNV_PRIME; /* hash the NUL too, separating the tiles */
    }

    const bool changed = (h != digest_);
    digest_ = h;
    return changed;
}

void MacroTiles::draw(DrawCtx &d)
{
    if (state_ == nullptr)
        return;

    for (uint8_t i = 0; i < TILES; i++) {
        char keyBuf[KEY_SIZE];
        char value[VALUE_SIZE];
        setUp(i, keyBuf);
        composeValue(i, *state_, value, sizeof(value));
        ScratchTile &scratch = Scratch::get<ScratchTile>();
        scratch.value.setText(value);
        scratch.key.setArea(key_[i]);
        scratch.value.setArea(value_[i]);

        /* Clip to the tile as its container would. */
        const bool clip = !tile_[i].empty();
        if (clip)
            d.pushClip(tile_[i]);
        scratch.key.paintTree(d);
        scratch.value.paintTree(d);
        if (clip)
            d.popClip();
    }
}

} // namespace ortxui
