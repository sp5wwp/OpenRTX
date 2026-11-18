/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_VIEW_HPP
#define ORTX_UI_VIEW_HPP

#include <cstdint>
#include "core/Object.hpp"
#include "core/UiText.hpp"
#include "core/state.h"

namespace ortxui
{

class View;
class TopBar;
struct Event;

/**
 * What a View asks the Navigator to do after handling an event.
 */
enum class NavAction : uint8_t {
    None,      //< Stay on this view
    Push,      //< Open `target` as a child screen (ENTER-style drill-in)
    Pop,       //< Return to the parent screen (ESC-style back)
    PopToRoot, //< Unwind all the way to the home screen (e.g. channel picked)
};

/**
 * A navigation request returned by a View from onEvent(). Views are statically
 * allocated singletons, so a push carries the target View pointer directly and
 * needs no id registry or table.
 */
struct NavIntent {
    NavAction action = NavAction::None;
    View *target = nullptr;

    static NavIntent none()
    {
        return {};
    }
    static NavIntent push(View *v)
    {
        return { NavAction::Push, v };
    }
    static NavIntent pop()
    {
        return { NavAction::Pop, nullptr };
    }
    static NavIntent popToRoot()
    {
        return { NavAction::PopToRoot, nullptr };
    }
};

/**
 * Base class for a full-screen view.
 *
 * A View owns a Screen (its retained widget tree) plus the navigation and
 * state-binding behaviour wrapped around it. Concrete views are static
 * singletons; the Navigator drives their lifecycle and treats the top of its
 * stack as the active screen.
 */
class View
{
public:
    virtual ~View() = default;

    /** Called each time this view becomes the active (top-of-stack) screen. */
    virtual void onShow()
    {
    }

    /**
     * Announce this screen via voice prompts on entry (called by the Navigator
     * after onShow(), gated by state.settings.vpLevel). The default speaks the
     * registered top-bar title, which covers every titled screen; views with
     * richer content (VFO summary, menu title+item, confirm prompt) override it.
     * List navigation is announced by the List itself, not here.
     */
    virtual void announce();

    /**
     * Pull displayed fields from the state snapshot (change-gated). The base
     * refreshes the shared top bar (clock/battery) if one was registered;
     * overrides should call it first, then sync their own content.
     */
    virtual void syncFromState(const state_t &s);

    /**
     * Handle a decoded input event and return a navigation intent. The default
     * routes the event into the Screen (focus/selection) and maps ESC to a
     * back-pop, which suits any leaf view.
     */
    virtual NavIntent onEvent(const Event &e);

    /** The view's widget-tree root. */
    virtual Screen &screen() = 0;

    /**
     * Consume a pending "reconfigure the radio" request. A view that edits an
     * rtx-affecting field of state.channel (e.g. FM CTCSS tones) calls
     * requestSyncRtx(); ui_shim reads this after dispatching an event and
     * raises the sync_rtx out-parameter so threads.c re-applies state.channel
     * to the radio. Returns true once, then clears the flag.
     */
    bool takeSyncRtx()
    {
        const bool v = syncRtx_;
        syncRtx_ = false;
        return v;
    }

protected:
    /** Register the shared top bar so the base syncFromState() refreshes it. */
    void setTopBar(TopBar *t)
    {
        topBar_ = t;
    }

    /** Ask ui_shim to reconfigure the radio from state.channel (see above). */
    void requestSyncRtx()
    {
        syncRtx_ = true;
    }

    /**
     * Speak a value/label via voice prompts (flush + announce + play), gated by
     * settings.vpLevel. Views call this from their edit paths so a value that
     * changes in place (no list-selection change) is still announced.
     *
     * @param text: text to speak; an entry plays its own prompt.
     */
    void vpSay(const UiText &text);

    TopBar *topBar_ = nullptr;

private:
    bool syncRtx_ = false;
};

} // namespace ortxui

#endif /* ORTX_UI_VIEW_HPP */
