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

namespace ortxui
{

void GpsView::build()
{
    const bool regular = (sizeClass() == SizeClass::Regular);

    root_.setArea({ 0, 0, static_cast<uint16_t>(CONFIG_SCREEN_WIDTH),
                    static_cast<uint16_t>(CONFIG_SCREEN_HEIGHT) });
    root_.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(gps));
    root_.addChild(&bar);

    /* Body: compass on the left, the readout column on the right. */
    body_.setAxis(Axis::Row);
    body_.setAlign(Align::Stretch);
    body_.setGrow(1);

    compass_.setBasis(regular ? 64 : 52);
    readout_.setGrow(1);

    body_.addChild(&compass_);
    body_.addChild(&readout_);
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

    if (readout_.sync(g))
        readout_.invalidate();

    if (g.tmg_true != lastHeading_) {
        compass_.setHeading(g.tmg_true, true);
        compass_.invalidate();
        lastHeading_ = g.tmg_true;
    }
}

void GpsView::announce()
{
    /* vp_announceGPSInfo() exists only with CONFIG_GPS, as this screen. */
#ifdef CONFIG_GPS
    if (state.settings.vpLevel >= vpLow)
        vp_announceGPSInfo(vpGPSAll);
#endif
}

} // namespace ortxui
