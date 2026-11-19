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

    root.setArea({ 0, 0, static_cast<uint16_t>(CONFIG_SCREEN_WIDTH),
                   static_cast<uint16_t>(CONFIG_SCREEN_HEIGHT) });
    root.setAxis(Axis::Column);

    TopBar &bar = useTopBar(STR(gps));
    root.addChild(&bar);

    /* Body: compass on the left, the readout column on the right. */
    body.setAxis(Axis::Row);
    body.setAlign(Align::Stretch);
    body.setGrow(1);

    compass.setBasis(regular ? 64 : 52);
    readout.setGrow(1);

    body.addChild(&compass);
    body.addChild(&readout);
    /* Start collapsed: the default presentation is "no fix" (see lastKind),
     * so the body is hidden until a fix arrives and showFixed() reveals it. */
    body.setFlag(FlagHidden, true);
    root.addChild(&body);

    /* No-fix status line (shown instead of the body when there is no fix). */
    status.setFont(regular ? FONT_SIZE_10PT : FONT_SIZE_8PT);
    status.setAlign(TEXT_ALIGN_CENTER);
    status.setColor(Sem::OnSurfaceMuted);
    status.setText(STR(noGps));
    status.setGrow(1);
    root.addChild(&status);

    tree.addChild(&root);
    root.onLayout();
    tree.markAllDirty();
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
    body.setFlag(FlagHidden, !fixed);
    status.setFlag(FlagHidden, fixed);
    root.onLayout(); /* re-flow now that one branch collapsed */
    tree.markAllDirty();
}

void GpsView::syncFromState(const state_t &s)
{
    View::syncFromState(s); /* shared top bar */

    const FixKind kind = classify(s);
    const bool fixed = (kind == FixKind::Fixed);

    if (kind != lastKind) {
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
            status.setText(msg);
            status.invalidate();
        }
        /* Toggle only when crossing the fixed/not-fixed boundary. */
        if (fixed != (lastKind == FixKind::Fixed))
            showFixed(fixed);
        lastKind = kind;
    }

    if (!fixed)
        return;

    const gps_t &g = s.gps_data;

    if (readout.sync(g))
        readout.invalidate();

    if (g.tmg_true != lastHeading) {
        compass.setHeading(g.tmg_true, true);
        compass.invalidate();
        lastHeading = g.tmg_true;
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
