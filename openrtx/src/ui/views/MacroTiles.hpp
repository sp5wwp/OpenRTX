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

#include <cstdint>
#include "core/Object.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The 3x3 tile grid of the macro menu: each tile shows its number key (with
 * an icon on the larger screens) and the live value of its action. The tiles
 * are painted one at a time through a shared scratch tile, and the values are
 * composed from the state snapshot when drawn, so the grid stores only where
 * each tile and its two labels go.
 */
class MacroTiles : public Object
{
public:
    /** Number of tiles. */
    static constexpr uint8_t TILES = 9;

    /** Constructor. */
    MacroTiles();

    /** Destructor. */
    ~MacroTiles();

    /**
     * Place the tiles over the three grid rows, as a row of three growing
     * tiles with their key and value labels in each row.
     *
     * @param rows: areas of the three rows.
     */
    void place(const Rect *rows);

    /**
     * Follow a state snapshot.
     *
     * @param s: state snapshot, which must outlive the next draw.
     * @return true if a tile value changed.
     */
    bool sync(const state_t &s);

    /** Paint the tiles. */
    void draw(DrawCtx &d) override;

private:
    /**
     * Set the scratch tile up for one tile, with an empty value.
     *
     * @param tile: tile index.
     * @param keyBuf: storage for the key text, at least 16 bytes.
     */
    static void setUp(uint8_t tile, char *keyBuf);

    /**
     * Compose a tile's value.
     *
     * @param tile: tile index.
     * @param s: state snapshot.
     * @param out: output buffer.
     * @param n: output buffer size.
     */
    static void composeValue(uint8_t tile, const state_t &s, char *out,
                             uint16_t n);

    Rect tile_[TILES];               ///< Area of each tile
    Rect key_[TILES];                ///< Area of each key label
    Rect value_[TILES];              ///< Area of each value label
    const state_t *state_ = nullptr; ///< Snapshot the values come from
    uint32_t digest_ = 0;            ///< Digest of the values shown
};

} // namespace ortxui

#endif /* ORTX_UI_MACROTILES_HPP */
