/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_MACROTILES_HPP
#define ORTX_UI_MACROTILES_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>
#include "base/Object.hpp"
#include "core/state.h"

namespace ortxui
{

/** Macro menu actions in tile order: number key N runs action N - 1. */
enum class MacroAction : uint8_t {
    ToneEnable,     ///< Cycle the CTCSS use
    ToneDown,       ///< Step the CTCSS tone down
    ToneUp,         ///< Step the CTCSS tone up
    Bandwidth,      ///< Toggle the FM bandwidth
    Mode,           ///< Switch the operating mode
    Power,          ///< Cycle the TX power
    BrightnessDown, ///< Dim the backlight
    BrightnessUp,   ///< Brighten the backlight
    KeypadLock,     ///< Toggle the keypad lock
    Count,          ///< Number of actions
};

/**
 * The 3x3 tile grid of the macro menu: each tile shows its number key (with
 * an icon on the larger screens) and the live value of its action. The tiles
 * are laid out and painted one at a time through the scratch tile, and the
 * values are composed when drawn from the readings the grid copies.
 */
class MacroTiles : public Object
{
public:
    /** Number of tiles. */
    static constexpr uint8_t TILES = static_cast<uint8_t>(MacroAction::Count);

    /** Constructor. */
    MacroTiles() = default;

    /** Destructor. */
    ~MacroTiles() = default;

    /**
     * Follow a state snapshot.
     *
     * @param s: state snapshot.
     * @return true if a tile value changed.
     */
    bool sync(const state_t &s);

    /**
     * Highlight a tile, on radios that pick the tile with the knob.
     *
     * @param tile: tile index, TILES for none.
     */
    void select(uint8_t tile)
    {
        selected_ = tile;
    }

    /** Paint the tiles. */
    void draw(DrawCtx &d) override;

private:
    /** What the tiles show, copied from the state snapshot. */
    struct Readings {
        uint32_t power;     ///< TX power, in mW
        fmInfo_t fm;        ///< CTCSS settings
        uint8_t mode;       ///< Operating mode
        uint8_t bandwidth;  ///< FM bandwidth
        uint8_t brightness; ///< Backlight level, in percent
        bool locked;        ///< Keypad locked
    };

    /**
     * Get the area of a tile: the grid is three rows sharing its height, each
     * of three equal tiles, TILE_GAP apart.
     *
     * @param tile: tile index.
     * @return the area, empty if the grid has no room.
     */
    Rect tileArea(uint8_t tile) const;

    /**
     * Set the scratch tile up for one tile.
     *
     * @param tile: tile index.
     * @param keyBuf: storage for the key text, at least 16 bytes.
     * @param value: value text, borrowed until the tile is painted.
     */
    static void setUp(uint8_t tile, char *keyBuf, const char *value);

    /**
     * Compose a tile's value.
     *
     * @param action: action of the tile.
     * @param r: readings shown.
     * @param out: output buffer.
     * @param n: output buffer size.
     */
    static void composeValue(MacroAction action, const Readings &r, char *out,
                             size_t n);

    Readings shown_ = {};      ///< Readings the tiles show
    uint8_t selected_ = TILES; ///< Highlighted tile, TILES for none
};

} // namespace ortxui

#endif /* ORTX_UI_MACROTILES_HPP */
