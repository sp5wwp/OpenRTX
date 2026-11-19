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
 * The key/value table of the Info screen: battery, charge, RSSI, heap, bands
 * and hardware version, one row each, every other row striped. The rows share
 * the table's height, and each is laid out when drawn: a key at its natural
 * width and the value right-aligned in the rest. The values are composed when
 * drawn too, so the table stores only the live readings it shows.
 */
class InfoTable : public Object
{
public:
    /** Rows, top to bottom. */
    enum Row : uint8_t {
        ROW_BATTERY,
        ROW_CHARGE,
        ROW_RSSI,
        ROW_HEAP,
        ROW_BAND,
        ROW_HW_VERSION,
        ROW_COUNT,
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

    uint32_t heapUsed_; ///< Heap in use shown
    int32_t rssi_;      ///< RSSI shown
    uint16_t vbat_;     ///< Battery voltage shown
    uint8_t charge_;    ///< Battery charge shown
};

} // namespace ortxui

#endif /* ORTX_UI_INFOTABLE_HPP */
