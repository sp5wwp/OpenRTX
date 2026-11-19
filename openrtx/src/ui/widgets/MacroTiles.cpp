/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/MacroTiles.hpp"
#include "base/UiFormat.hpp"
#include "base/UiText.hpp"
#include "core/cps.h"
#include "core/graphics.h"
#include "layout/Flex.hpp"
#include "render/DrawCtx.hpp"
#include "style/Symbols.hpp"
#include "widgets/Scratch.hpp"
#include "widgets/Label.hpp"
#include "hwconfig.h"

#include <cstdio>
#include <cstring>

namespace ortxui
{

/** Key label of each tile on a regular screen: the key and an icon or tag. */
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

/** Key label of each tile on a compact screen, where icons do not fit. */
static constexpr const char *DIGITS[MacroTiles::TILES] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9",
};

static constexpr uint8_t TILE_GAP = 2;       ///< Gap between the tiles of a row
static constexpr int16_t KEY_H_REGULAR = 12; ///< Key line of a two-line tile
static constexpr int16_t KEY_W_COMPACT = 9;  ///< Key width of a one-line tile
static constexpr uint8_t TILE_PAD_X = 2;     ///< Horizontal padding in a tile
static constexpr uint8_t TILE_PAD_Y = 1;     ///< Vertical padding in a tile
static constexpr size_t VALUE_SIZE = 14;     ///< Size of a composed value
static constexpr uint16_t KEY_SIZE = 16;     ///< Size of a composed key label

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

    /* The key over the value on a regular screen, beside it on a compact
     * one. */
    scratch.box.setAxis(regular ? Axis::Column : Axis::Row);
    scratch.box.setAlign(Align::Center);
    scratch.box.setPadding(TILE_PAD_X, TILE_PAD_Y);

    if (KEY_LABELS[tile] != nullptr)
        sniprintf(keyBuf, KEY_SIZE, "%s", KEY_LABELS[tile]);
    else
        sniprintf(keyBuf, KEY_SIZE, "5 %s", STR(mode).str());

    scratch.key.setFont(regular ? FONT_SIZE_6PT : FONT_SIZE_5PT);
    scratch.key.setAlign(regular ? TEXT_ALIGN_CENTER : TEXT_ALIGN_LEFT);
    scratch.key.setColor(Sem::Accent);
    scratch.key.setText(regular ? keyBuf : DIGITS[tile]);
    scratch.key.setBasis(regular ? KEY_H_REGULAR : KEY_W_COMPACT);

    scratch.value.setFont(regular ? FONT_SIZE_8PT : FONT_SIZE_6PT);
    scratch.value.setAlign(regular ? TEXT_ALIGN_CENTER : TEXT_ALIGN_RIGHT);
    scratch.value.setColor(Sem::OnSurface);
    scratch.value.setText(value);
    scratch.value.setGrow(1);
}

Rect MacroTiles::tileArea(uint8_t tile) const
{
    const uint16_t rowH = static_cast<uint16_t>(bounds.h / 3);
    const Rect row = { bounds.x,
                       static_cast<int16_t>(bounds.y + (tile / 3) * rowH),
                       bounds.w, rowH };
    if ((row.w == 0) || (row.h == 0))
        return { 0, 0, 0, 0 };

    int free = static_cast<int>(row.w) - 2 * TILE_GAP;
    if (free < 0)
        free = 0;
    const uint16_t w = static_cast<uint16_t>(free / 3);
    return { static_cast<int16_t>(row.x + (tile % 3) * (w + TILE_GAP)), row.y,
             w, row.h };
}

void MacroTiles::composeValue(MacroAction action, const Readings &r, char *out,
                              size_t n)
{
    const bool fm = (r.mode == OPMODE_FM);

    switch (action) {
        case MacroAction::ToneEnable:
            sniprintf(out, n, "%s", fm ? toneEnableLabel(r.fm).str() : "-");
            break;
        case MacroAction::ToneDown:
        case MacroAction::ToneUp:
            if (fm)
                formatTone(r.fm.txTone, out, n);
            else
                sniprintf(out, n, "-");
            break;
        case MacroAction::Bandwidth:
            sniprintf(out, n, "%s", fm ? bandwidthLabel(r.bandwidth) : "-");
            break;
        case MacroAction::Mode:
            sniprintf(out, n, "%s", modeLabel(r.mode).str());
            break;
        case MacroAction::Power:
            formatPower(r.power, out, n);
            break;
        case MacroAction::BrightnessDown:
        case MacroAction::BrightnessUp:
#ifdef CONFIG_SCREEN_BRIGHTNESS
            sniprintf(out, n, "%u%%", static_cast<unsigned>(r.brightness));
#else
            sniprintf(out, n, "-");
#endif
            break;
        case MacroAction::KeypadLock:
        default:
            sniprintf(out, n, "%s", (r.locked ? STR(on) : STR(off)).str());
            break;
    }
}

bool MacroTiles::sync(const state_t &s, bool locked)
{
    const channel_t &ch = s.channel;
    Readings r = {};
    r.power = ch.power;
    r.fm = ch.fm;
    r.mode = ch.mode;
    r.bandwidth = ch.bandwidth;
    r.brightness = s.settings.brightness;
    r.locked = locked;

    const bool changed = (r.power != shown.power)
                      || (memcmp(&r.fm, &shown.fm, sizeof(r.fm)) != 0)
                      || (r.mode != shown.mode)
                      || (r.bandwidth != shown.bandwidth)
                      || (r.brightness != shown.brightness)
                      || (r.locked != shown.locked);
    shown = r;
    return changed;
}

void MacroTiles::draw(DrawCtx &d)
{
    for (uint8_t i = 0; i < TILES; i++) {
        char keyBuf[KEY_SIZE];
        char value[VALUE_SIZE];
        const Rect tile = tileArea(i);
        /* A tile outside the clip, as in another band, draws nothing. */
        if (!tile.empty() && intersect(d.clip(), tile).empty())
            continue;
        ScratchTile &scratch = Scratch::get<ScratchTile>();

        composeValue(static_cast<MacroAction>(i), shown, value, sizeof(value));
        setUp(i, keyBuf, value);
        scratch.key.setArea({ 0, 0, 0, 0 });
        scratch.value.setArea({ 0, 0, 0, 0 });
        if (!tile.empty()) {
            scratch.box.setArea(tile);
            scratch.box.onLayout();
        }

        const bool selected = (i == selectedTile);
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
