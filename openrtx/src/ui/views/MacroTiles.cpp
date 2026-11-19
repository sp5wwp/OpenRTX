/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/MacroTiles.hpp"
#include "core/UiMemory.hpp"
#include "core/UiFormat.hpp"
#include "core/UiText.hpp"
#include "core/cps.h"
#include "core/graphics.h"
#include "core/utils.h"
#include "layout/Flex.hpp"
#include "render/DrawCtx.hpp"
#include "style/Symbols.hpp"
#include "widgets/Scratch.hpp"
#include "widgets/Label.hpp"
#include "hwconfig.h"

#include <cstdio>

namespace ortxui
{

/* Top-of-tile labels (number key + icon / short label), one per key 1-9. */
static constexpr const char *KEY_LABELS[MacroTiles::TILES] = {
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
static constexpr const char *DIGITS[MacroTiles::TILES] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9",
};

/* Gap between the tiles of a row. */
static constexpr uint8_t TILE_GAP = 2;

/* Size of a composed tile value. */
static constexpr uint16_t VALUE_SIZE = 14;

/* Size of a composed key label. */
static constexpr uint16_t KEY_SIZE = 16;

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

void MacroTiles::setUp(uint8_t tile, char *keyBuf, const char *value)
{
    const bool regular = (sizeClass() == SizeClass::Regular);
    ScratchTile &scratch = Scratch::get<ScratchTile>();

    /* Regular: a 2-line tile (number + icon over the value). Compact (mono):
     * a single row "N value" — icons and a second line don't fit on 128x64. */
    scratch.box.setAxis(regular ? Axis::Column : Axis::Row);
    scratch.box.setAlign(Align::Center);
    scratch.box.setPadding(2, 1);

    if (KEY_LABELS[tile] != nullptr)
        sniprintf(keyBuf, KEY_SIZE, "%s", KEY_LABELS[tile]);
    else
        sniprintf(keyBuf, KEY_SIZE, "5 %s", STR(mode).str());

    scratch.key.setFont(regular ? FONT_SIZE_6PT : FONT_SIZE_5PT);
    scratch.key.setAlign(regular ? TEXT_ALIGN_CENTER : TEXT_ALIGN_LEFT);
    scratch.key.setColor(Sem::Accent);
    scratch.key.setText(regular ? keyBuf : DIGITS[tile]);
    scratch.key.setBasis(regular ? 12 : 9);

    scratch.value.setFont(regular ? FONT_SIZE_8PT : FONT_SIZE_6PT);
    scratch.value.setAlign(regular ? TEXT_ALIGN_CENTER : TEXT_ALIGN_RIGHT);
    scratch.value.setColor(Sem::OnSurface);
    scratch.value.setText(value);
    scratch.value.setGrow(1);
}

Rect MacroTiles::tileArea(uint8_t tile) const
{
    const uint16_t rowH = static_cast<uint16_t>(area_.h / 3);
    const Rect row = { area_.x,
                       static_cast<int16_t>(area_.y + (tile / 3) * rowH),
                       area_.w, rowH };
    if ((row.w == 0) || (row.h == 0))
        return { 0, 0, 0, 0 };

    int free = static_cast<int>(row.w) - 2 * TILE_GAP;
    if (free < 0)
        free = 0;
    const uint16_t w = static_cast<uint16_t>(free / 3);
    return { static_cast<int16_t>(row.x + (tile % 3) * (w + TILE_GAP)), row.y,
             w, row.h };
}

void MacroTiles::composeValue(uint8_t tile, const state_t &s, char *out,
                              uint16_t n)
{
    const channel_t &ch = s.channel;
    const bool fm = (ch.mode == OPMODE_FM);

    switch (tile) {
        case 0: /* tone encode/decode */
            sniprintf(out, n, "%s", fm ? toneEnableLabel(ch.fm).str() : "-");
            break;
        case 1: /* tone down (shows the tone) */
        case 2: /* tone up */
            if (fm)
                formatTone(ch.fm.txTone, out, n);
            else
                sniprintf(out, n, "-");
            break;
        case 3: /* bandwidth */
            sniprintf(out, n, "%s", fm ? bandwidthLabel(ch.bandwidth) : "-");
            break;
        case 4: /* radio mode */
            sniprintf(out, n, "%s", modeLabel(ch.mode).str());
            break;
        case 5: /* power */
            formatPower(ch.power, out, n);
            break;
        case 6: /* brightness */
        case 7:
#ifdef CONFIG_SCREEN_BRIGHTNESS
            sniprintf(out, n, "%u%%", (unsigned)s.settings.brightness);
#else
            sniprintf(out, n, "-");
#endif
            break;
        case 8: /* keypad lock */
            sniprintf(out, n, "%s",
                      (uiMemory.keypadLocked ? STR(on) : STR(off)).str());
            break;
        default:
            out[0] = '\0';
            break;
    }
}

bool MacroTiles::sync(const state_t &s)
{
    state_ = &s;

    uint32_t h = FNV1A_BASIS;
    for (uint8_t i = 0; i < TILES; i++) {
        char value[VALUE_SIZE];
        composeValue(i, s, value, sizeof(value));
        for (const char *c = value; *c != '\0'; c++)
            h = fnv1a(h, static_cast<uint8_t>(*c));
        h = fnv1a(h, 0u); /* hash the NUL too, separating the tiles */
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
        const Rect tile = tileArea(i);
        /* A tile outside the clip, as in another band, draws nothing. */
        if (!tile.empty() && intersect(d.clip(), tile).empty())
            continue;
        ScratchTile &scratch = Scratch::get<ScratchTile>();

        composeValue(i, *state_, value, sizeof(value));
        setUp(i, keyBuf, value);
        scratch.key.setArea({ 0, 0, 0, 0 });
        scratch.value.setArea({ 0, 0, 0, 0 });
        if (!tile.empty()) {
            scratch.box.setArea(tile);
            scratch.box.onLayout();
        }

        const bool selected = (i == selected_);
        if (selected)
            d.fillRect(tile, Sem::Primary);
        scratch.key.setColor(selected ? Sem::OnPrimary : Sem::Accent);
        scratch.value.setColor(selected ? Sem::OnPrimary : Sem::OnSurface);

        /* Clip to the tile as its container would. */
        const bool clip = !tile.empty();
        if (clip)
            d.pushClip(tile);
        scratch.key.paintTree(d);
        scratch.value.paintTree(d);
        if (clip)
            d.popClip();
    }
}

} // namespace ortxui
