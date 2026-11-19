/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_GPSREADOUT_HPP
#define ORTX_UI_GPSREADOUT_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <cstddef>
#include <cstdint>
#include "base/Object.hpp"
#include "core/gps.h"
#include "widgets/TextRow.hpp"

namespace ortxui
{

/**
 * The readout column of the GPS screen: latitude, longitude, locator,
 * altitude and speed, a big value and a small unit per row. The rows are laid
 * out as a padded column of equally growing rows and painted one at a time
 * through one TextRow, with the text composed from the fix when drawn, so the
 * column stores only the row height and the values it shows.
 */
class GpsReadout : public Object
{
public:
    /** Constructor. */
    GpsReadout();

    /** Destructor. */
    ~GpsReadout() = default;

    /**
     * Follow a fix.
     *
     * @param g: GPS data.
     * @return true if a shown value changed.
     */
    bool sync(const gps_t &g);

    /** Size the rows to the column. */
    void onLayout() override;

    /** Paint the rows. */
    void draw(DrawCtx &d) override;

private:
    /**
     * Compose a row's value and unit.
     *
     * @param row: row.
     * @param val: output buffer for the value.
     * @param n: size of the output buffer.
     * @return the unit.
     */
    const char *composeRow(uint8_t row, char *val, size_t n) const;

    /** Rows, top to bottom. */
    enum Row : uint8_t {
        RowLat,   ///< Latitude
        RowLon,   ///< Longitude
        RowLoc,   ///< Maidenhead locator
        RowAlt,   ///< Altitude
        RowSpd,   ///< Speed
        RowCount, ///< Number of rows
    };

    TextRow textRow;    ///< Paints each row in turn
    int32_t lat = 0;    ///< Latitude shown
    int32_t lon = 0;    ///< Longitude shown
    int16_t alt = 0;    ///< Altitude shown
    uint16_t speed = 0; ///< Speed shown
    uint16_t rowH = 0;  ///< Height of each row, 0 before a layout
};

} // namespace ortxui

#endif /* ORTX_UI_GPSREADOUT_HPP */
