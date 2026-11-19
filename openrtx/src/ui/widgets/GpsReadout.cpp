/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/GpsReadout.hpp"
#include "base/Layout.hpp"
#include "base/UiText.hpp"

#include <climits>
#include <cmath>
#include <cstdio>

namespace ortxui
{

/* Inset of the rows from the column edges. */
static constexpr int16_t PAD = 2;

/* Size of a composed value. */
static constexpr size_t VALUE_SIZE = 16;

/* Label of each row, in row order; the hemisphere replaces the first two once
 * there is a fix. */
static constexpr UiText UNITS[] = { UiText("N"), UiText("E"), STR(locator),
                                    STR(altitudeShort), STR(speedShort) };

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

    sniprintf(val, valSize, "%ld %ld.%02ld'", (long)deg, (long)(min100 / 100),
              (long)(min100 % 100));

    if (isLat)
        return (coord < 0) ? "S" : "N";
    return (coord < 0) ? "W" : "E";
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
    double a = static_cast<double>(lon) / 1000000.0 + 180.0;
    double b = static_cast<double>(lat) / 1000000.0 + 90.0;

    /* Clamp to the valid grid to keep the arithmetic in range. */
    if (a < 0.0)
        a = 0.0;
    if (a >= 360.0)
        a = 359.999;
    if (b < 0.0)
        b = 0.0;
    if (b >= 180.0)
        b = 179.999;

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

GpsReadout::GpsReadout() :
    lat(INT32_MIN), lon(INT32_MIN), alt(INT16_MIN), speed(UINT16_MAX), rowH(0)
{
    const bool regular = (sizeClass() == SizeClass::Regular);

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
    /* A padded column whose rows grow equally from their line height. A
     * column with no room keeps the rows it had. */
    const int cw = static_cast<int>(bounds.w) - 2 * PAD;
    const int ch = static_cast<int>(bounds.h) - 2 * PAD;
    if ((cw <= 0) || (ch <= 0))
        return;

    const int lineH = textRow.natural().h;
    int free = ch - ROW_COUNT * lineH;
    if (free < 0)
        free = 0;
    rowH = static_cast<uint16_t>(lineH + free / ROW_COUNT);
}

const char *GpsReadout::composeRow(uint8_t row, char *val, size_t n) const
{
    const bool fixed = (lat != INT32_MIN) || (lon != INT32_MIN);

    switch (row) {
        case ROW_LAT:
            if (fixed)
                return formatCoord(lat, true, val, n);
            break;
        case ROW_LON:
            if (fixed)
                return formatCoord(lon, false, val, n);
            break;
        case ROW_LOC:
            if (fixed) {
                formatLocator(lat, lon, val, n);
                return UNITS[row].str();
            }
            break;
        case ROW_ALT:
            if (alt != INT16_MIN) {
                sniprintf(val, n, "%d%s", alt, STR(metres).str());
                return UNITS[row].str();
            }
            break;
        default:
            if (speed != UINT16_MAX) {
                sniprintf(val, n, "%u%s", speed, STR(kmPerHour).str());
                return UNITS[row].str();
            }
            break;
    }

    sniprintf(val, n, "--");
    return UNITS[row].str();
}

void GpsReadout::draw(DrawCtx &d)
{
    const int16_t x = static_cast<int16_t>(bounds.x + PAD);
    const uint16_t w = static_cast<uint16_t>(bounds.w - 2 * PAD);

    for (uint8_t i = 0; i < ROW_COUNT; i++) {
        char val[VALUE_SIZE];
        textRow.setText(1, composeRow(i, val, sizeof(val)));
        textRow.setText(0, val);
        textRow.setArea(
            { x, static_cast<int16_t>(bounds.y + PAD + i * rowH), w, rowH });
        textRow.paintTree(d);
    }

    /* The value lived on this stack frame: do not leave the row pointing at
     * it for the next layout to measure. */
    textRow.setText(0, "");
}

} // namespace ortxui
