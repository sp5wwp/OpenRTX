/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/GpsReadout.hpp"
#include "base/Layout.hpp"
#include "base/UiText.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ortxui
{

static constexpr int16_t PAD = 2;        ///< Inset of the rows in the column
static constexpr size_t VALUE_SIZE = 16; ///< Size of a composed value

/** Unit of the rows after the coordinates, which show their hemisphere. */
static constexpr UiText UNITS[] = { STR(locator), STR(altitudeShort),
                                    STR(speedShort) };

/**
 * Format a signed coordinate (millionths of a degree) as degrees and decimal
 * minutes, e.g. 33840333 -> "33 50.42'". The font lacks a degree glyph, so a
 * space stands in for it; the trailing prime marks minutes.
 *
 * @param coord: coordinate.
 * @param isLat: whether it is a latitude.
 * @param val: output buffer.
 * @param valSize: size of the output buffer.
 * @return the hemisphere letter.
 */
static const char *formatCoord(int32_t coord, bool isLat, char *val,
                               size_t valSize)
{
    const int32_t mag = (coord < 0) ? -coord : coord;
    const int32_t deg = mag / 1000000;
    const int32_t frac = mag % 1000000;
    /* minutes * 100, rounded: frac/1e6 * 60 * 100 = frac * 6 / 1000. */
    const int32_t min100 = (frac * 6 + 500) / 1000;

    sniprintf(val, valSize, "%ld %ld.%02ld'", static_cast<long>(deg),
              static_cast<long>(min100 / 100), static_cast<long>(min100 % 100));

    if (isLat)
        return ((coord < 0) ? STR(south) : STR(north)).str();
    return ((coord < 0) ? STR(west) : STR(east)).str();
}

/**
 * Format the 6-character Maidenhead locator of a position.
 *
 * @param lat: latitude, in millionths of a degree.
 * @param lon: longitude, in millionths of a degree.
 * @param out: output buffer.
 * @param outSize: size of the output buffer.
 */
static void formatLocator(int32_t lat, int32_t lon, char *out, size_t outSize)
{
    /* Clamp to the valid grid to keep the arithmetic in range. */
    double a = static_cast<double>(lon) / 1000000.0 + 180.0;
    double b = static_cast<double>(lat) / 1000000.0 + 90.0;
    a = std::min(std::max(a, 0.0), 359.999);
    b = std::min(std::max(b, 0.0), 179.999);

    char loc[7];
    loc[0] = static_cast<char>('A' + static_cast<int>(a / 20.0));
    loc[1] = static_cast<char>('A' + static_cast<int>(b / 10.0));
    a = fmod(a, 20.0);
    b = fmod(b, 10.0);
    loc[2] = static_cast<char>('0' + static_cast<int>(a / 2.0));
    loc[3] = static_cast<char>('0' + static_cast<int>(b));
    a = fmod(a, 2.0);
    b = fmod(b, 1.0);
    loc[4] = static_cast<char>('A' + static_cast<int>(a * 12.0));
    loc[5] = static_cast<char>('A' + static_cast<int>(b * 24.0));
    loc[6] = '\0';

    sniprintf(out, outSize, "%s", loc);
}

GpsReadout::GpsReadout()
{
    const bool regular = (sizeClass() >= SizeClass::Regular);

    /* A value and a small unit, right-aligned and sharing one baseline. */
    textRow.setRun(0, regular ? FONT_SIZE_8PT : FONT_SIZE_6PT, Sem::OnSurface);
    textRow.setRun(1, FONT_SIZE_6PT, Sem::OnSurfaceMuted);
}

bool GpsReadout::sync(const gps_t &g)
{
    const bool changed = (g.latitude != lat) || (g.longitude != lon)
                      || (g.altitude != alt) || (g.speed != speed);

    lat = g.latitude;
    lon = g.longitude;
    alt = g.altitude;
    speed = g.speed;
    return changed;
}

void GpsReadout::onLayout()
{
    /* The rows grow equally; a column with no room keeps the rows it had. */
    const int cw = static_cast<int>(bounds.w) - 2 * PAD;
    const int ch = static_cast<int>(bounds.h) - 2 * PAD;
    if ((cw <= 0) || (ch <= 0))
        return;

    const int32_t lineH = textRow.natural().h;
    const int32_t spare = std::max<int32_t>(ch - RowCount * lineH, 0);
    rowH = static_cast<uint16_t>(lineH + spare / RowCount);
}

const char *GpsReadout::composeRow(uint8_t row, char *val, size_t n) const
{
    switch (row) {
        case RowLat:
            return formatCoord(lat, true, val, n);
        case RowLon:
            return formatCoord(lon, false, val, n);
        case RowLoc:
            formatLocator(lat, lon, val, n);
            break;
        case RowAlt:
            sniprintf(val, n, "%d%s", alt, STR(metres).str());
            break;
        default:
            sniprintf(val, n, "%u%s", speed, STR(kmPerHour).str());
            break;
    }

    return UNITS[row - RowLoc].str();
}

void GpsReadout::draw(DrawCtx &d)
{
    const int16_t x = static_cast<int16_t>(bounds.x + PAD);
    const uint16_t w = static_cast<uint16_t>(bounds.w - 2 * PAD);

    for (uint8_t i = 0; i < RowCount; i++) {
        char val[VALUE_SIZE];
        textRow.setText(1, composeRow(i, val, sizeof(val)));
        textRow.setText(0, val);
        textRow.setArea(
            { x, static_cast<int16_t>(bounds.y + PAD + i * rowH), w, rowH });
        textRow.paintTree(d);
    }

    /* Do not leave the row pointing at this stack frame. */
    textRow.setText(0, "");
}

} // namespace ortxui
