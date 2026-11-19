/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_INFOTABLE_HPP
#define ORTX_UI_INFOTABLE_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>
#include "base/Object.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The key/value table of the Info screen, one row per reading, every other
 * row striped. The values are composed when drawn, so the table stores only
 * the live readings it shows.
 */
class InfoTable : public Object
{
public:
    /** Rows, top to bottom. */
    enum Row : uint8_t {
        RowBattery,   ///< Battery voltage
        RowCharge,    ///< Battery charge
        RowRssi,      ///< RSSI
        RowHeap,      ///< Heap in use
        RowBand,      ///< Bands the radio covers
        RowHwVersion, ///< Hardware version
        RowCount,     ///< Number of rows
    };

    /** Constructor. */
    InfoTable();

    /** Destructor. */
    ~InfoTable() = default;

    /**
     * Follow a state snapshot.
     *
     * @param s: state snapshot.
     * @return true if a value changed.
     */
    bool sync(const state_t &s);

    /** Paint the stripes, keys and values. */
    void draw(DrawCtx &d) override;

private:
    /**
     * Paint one row.
     *
     * @param d: drawing context.
     * @param i: row.
     * @param row: area of the row.
     */
    void drawRow(DrawCtx &d, uint8_t i, const Rect &row) const;

    /**
     * Compose a row's value.
     *
     * @param row: row.
     * @param out: output buffer.
     * @param n: output buffer size.
     */
    void composeValue(uint8_t row, char *out, size_t n) const;

    uint32_t heapUsed; ///< Heap in use shown
    int32_t rssi;      ///< RSSI shown
    uint16_t vbat;     ///< Battery voltage shown
    uint8_t charge;    ///< Battery charge shown
};

} // namespace ortxui

#endif /* ORTX_UI_INFOTABLE_HPP */
