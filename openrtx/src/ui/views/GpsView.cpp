/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/GpsView.hpp"
#include "core/state.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"
#include "hwconfig.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ortxui
{

namespace
{

/* Format a signed coordinate (millionths of a degree) as degrees + decimal
 * minutes, e.g. 33840333 -> "33 50.42". The bitmap font lacks a degree glyph,
 * so a space stands in for it; the trailing prime marks minutes. `dir` receives
 * the hemisphere letter. */
void formatCoord(int32_t coord, bool isLat, char *val, size_t valSize,
                 const char **dir)
{
    const int32_t mag = (coord < 0) ? -coord : coord;
    const int32_t deg = mag / 1000000;
    const int32_t frac = mag % 1000000;
    /* minutes * 100, rounded: frac/1e6 * 60 * 100 = frac * 6 / 1000. */
    const int32_t min100 = (frac * 6 + 500) / 1000;

    snprintf(val, valSize, "%ld %ld.%02ld'", (long)deg, (long)(min100 / 100),
             (long)(min100 % 100));

    if (isLat)
        *dir = (coord < 0) ? "S" : "N";
    else
        *dir = (coord < 0) ? "W" : "E";
}

/* Maidenhead 6-character locator from a lat/lon in millionths of a degree. */
void formatLocator(int32_t lat, int32_t lon, char *out, size_t outSize)
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

    snprintf(out, outSize, "%s", loc);
}

} // namespace

void GpsView::build()
{
    const int16_t W = CONFIG_SCREEN_WIDTH;
    const int16_t H = CONFIG_SCREEN_HEIGHT;
    const bool regular = (sizeClass() == SizeClass::Regular);
    const fontSize_t valFont = regular ? FONT_SIZE_8PT : FONT_SIZE_6PT;

    root_.setArea({ 0, 0, (uint16_t)W, (uint16_t)H });
    root_.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(gps));
    root_.addChild(&bar);

    /* Body: compass on the left, the readout column on the right. */
    body_.setAxis(Axis::Row);
    body_.setAlign(Align::Stretch);
    body_.setGrow(1);

    compass_.setBasis(regular ? 64 : 52);

    statsCol_.setAxis(Axis::Column);
    statsCol_.setAlign(Align::Stretch);
    statsCol_.setPadding(2, 2);
    statsCol_.setGrow(1);

    static const char *const kUnits[RowCount] = { "N", "E", "QTH", "ALT",
                                                  "SPD" };
    for (uint8_t i = 0; i < RowCount; i++) {
        /* value + small unit, right-aligned and sharing one baseline. */
        rows_[i].setRun(0, valFont, Sem::OnSurface, TextRow::Side::Right);
        rows_[i].setRun(1, FONT_SIZE_6PT, Sem::OnSurfaceMuted,
                        TextRow::Side::Right);
        rows_[i].setText(0, "--");
        rows_[i].setText(1, kUnits[i]);
        rows_[i].setGrow(1);
        statsCol_.addChild(&rows_[i]);
    }

    body_.addChild(&compass_);
    body_.addChild(&statsCol_);
    /* Start collapsed: the default presentation is "no fix" (see lastKind_),
     * so the body is hidden until a fix arrives and showFixed() reveals it. */
    body_.setFlag(FLAG_HIDDEN, true);
    root_.addChild(&body_);

    /* No-fix status line (shown instead of the body when there is no fix). */
    status_.setFont(regular ? FONT_SIZE_10PT : FONT_SIZE_8PT);
    status_.setAlign(TEXT_ALIGN_CENTER);
    status_.setColor(Sem::OnSurfaceMuted);
    status_.setText(STR(noGps));
    status_.setGrow(1);
    root_.addChild(&status_);

    screen_.addChild(&root_);
    root_.onLayout();
    screen_.markAllDirty();
}

GpsView::FixKind GpsView::classify(const state_t &s) const
{
    if (!s.gpsDetected)
        return FixKind::NoGps;
    if (!s.settings.gps_enabled)
        return FixKind::GpsOff;
    if (s.gps_data.fix_quality == FIX_QUALITY_NO_FIX)
        return FixKind::NoFix;
    if (s.gps_data.fix_quality == FIX_QUALITY_ESTIMATED)
        return FixKind::FixLost;
    return FixKind::Fixed;
}

void GpsView::showFixed(bool fixed)
{
    body_.setFlag(FLAG_HIDDEN, !fixed);
    status_.setFlag(FLAG_HIDDEN, fixed);
    root_.onLayout(); /* re-flow now that one branch collapsed */
    screen_.markAllDirty();
}

void GpsView::syncFromState(const state_t &s)
{
    View::syncFromState(s); /* shared top bar */

    const FixKind kind = classify(s);
    const bool fixed = (kind == FixKind::Fixed);

    if (kind != lastKind_) {
        if (!fixed) {
            UiText msg = STR(noFix);
            switch (kind) {
                case FixKind::NoGps:
                    msg = STR(noGps);
                    break;
                case FixKind::GpsOff:
                    msg = STR(gpsOff);
                    break;
                case FixKind::FixLost:
                    msg = STR(fixLost);
                    break;
                default:
                    break;
            }
            status_.setText(msg);
            status_.invalidate();
        }
        /* Toggle only when crossing the fixed/not-fixed boundary. */
        if (fixed != (lastKind_ == FixKind::Fixed))
            showFixed(fixed);
        lastKind_ = kind;
    }

    if (!fixed)
        return;

    const gps_t &g = s.gps_data;

    if (g.latitude != lastLat_ || g.longitude != lastLon_) {
        const char *dir = "";
        formatCoord(g.latitude, true, valBufs_[RowLat],
                    sizeof(valBufs_[RowLat]), &dir);
        rows_[RowLat].setText(0, valBufs_[RowLat]);
        rows_[RowLat].setText(1, dir);
        rows_[RowLat].invalidate();

        formatCoord(g.longitude, false, valBufs_[RowLon],
                    sizeof(valBufs_[RowLon]), &dir);
        rows_[RowLon].setText(0, valBufs_[RowLon]);
        rows_[RowLon].setText(1, dir);
        rows_[RowLon].invalidate();

        formatLocator(g.latitude, g.longitude, valBufs_[RowLoc],
                      sizeof(valBufs_[RowLoc]));
        rows_[RowLoc].setText(0, valBufs_[RowLoc]);
        rows_[RowLoc].invalidate();

        lastLat_ = g.latitude;
        lastLon_ = g.longitude;
    }

    if (g.altitude != lastAlt_) {
        snprintf(valBufs_[RowAlt], sizeof(valBufs_[RowAlt]), "%dm", g.altitude);
        rows_[RowAlt].setText(0, valBufs_[RowAlt]);
        rows_[RowAlt].invalidate();
        lastAlt_ = g.altitude;
    }

    if (g.speed != lastSpeed_) {
        snprintf(valBufs_[RowSpd], sizeof(valBufs_[RowSpd]), "%ukm/h", g.speed);
        rows_[RowSpd].setText(0, valBufs_[RowSpd]);
        rows_[RowSpd].invalidate();
        lastSpeed_ = g.speed;
    }

    if (g.tmg_true != lastHeading_) {
        compass_.setHeading(g.tmg_true, true);
        compass_.invalidate();
        lastHeading_ = g.tmg_true;
    }
}

void GpsView::announce()
{
    /* Speak the full GPS fix on entry (fix quality, position, speed, altitude,
     * direction) rather than just the screen title. vp_announceGPSInfo is only
     * declared under CONFIG_GPS; this view is only reachable there too. */
#ifdef CONFIG_GPS
    if (state.settings.vpLevel >= vpLow)
        vp_announceGPSInfo(vpGPSAll);
#endif
}

} // namespace ortxui
