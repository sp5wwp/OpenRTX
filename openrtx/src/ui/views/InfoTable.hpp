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
#include "core/Object.hpp"
#include "core/state.h"

namespace ortxui
{

/**
 * The key/value text of the Info screen: battery, charge, RSSI, heap, bands
 * and hardware version, one row each. The rows are laid out like a padded
 * row of a key at its natural width and a value filling the rest, and the
 * values are composed when drawn, so the table stores only where each text
 * goes and the live readings it shows.
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
    ~InfoTable();

    /**
     * Place the key and value of each row.
     *
     * @param rows: area of each row.
     */
    void place(const Rect *rows);

    /**
     * Follow a state snapshot.
     *
     * @param s: state snapshot.
     * @return true if a value changed.
     */
    bool sync(const state_t &s);

    /** Paint the keys and values. */
    void draw(DrawCtx &d) override;

private:
    /**
     * Compose a row's value.
     *
     * @param row: row.
     * @param out: output buffer.
     * @param n: output buffer size.
     */
    void composeValue(uint8_t row, char *out, size_t n) const;

    Rect row_[ROW_COUNT];   ///< Area of each row
    Rect key_[ROW_COUNT];   ///< Area of each key
    Rect value_[ROW_COUNT]; ///< Area of each value
    uint32_t heapUsed_;     ///< Heap in use shown
    int32_t rssi_;          ///< RSSI shown
    uint16_t vbat_;         ///< Battery voltage shown
    uint8_t charge_;        ///< Battery charge shown
};

} // namespace ortxui

#endif /* ORTX_UI_INFOTABLE_HPP */
