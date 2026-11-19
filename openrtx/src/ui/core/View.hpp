/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_VIEW_HPP
#define ORTX_UI_VIEW_HPP

#include <cstdint>
#include "core/Object.hpp"
#include "core/Screen.hpp"
#include "core/UiText.hpp"
#include "core/ViewId.hpp"
#include "core/state.h"

namespace ortxui
{

class TopBar;
class List;
struct Event;
struct UiMemory;

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
 * A navigation request returned by a View from onEvent(). Only one view exists
 * at a time, so a push names the screen to build.
 */
struct NavIntent {
    /**
     * Stay on the screen.
     *
     * @return the intent.
     */
    static NavIntent none()
    {
        return {};
    }

    /**
     * Open a child screen.
     *
     * @param id: screen to open.
     * @return the intent.
     */
    static NavIntent push(ViewId id)
    {
        return { NavAction::Push, id };
    }

    /**
     * Return to the parent screen.
     *
     * @return the intent.
     */
    static NavIntent pop()
    {
        return { NavAction::Pop, ViewId::Count };
    }

    /**
     * Return to the home screen.
     *
     * @return the intent.
     */
    static NavIntent popToRoot()
    {
        return { NavAction::PopToRoot, ViewId::Count };
    }

    NavAction action = NavAction::None; ///< What to do
    ViewId target = ViewId::Count;      ///< Screen a push opens
};

/**
 * Base class for a full-screen view.
 *
 * A View owns a Screen (its retained widget tree) plus the navigation and
 * state-binding behaviour wrapped around it. Only the active view exists: the
 * Navigator builds it in a shared slot and destroys it when another screen
 * replaces it, so anything that must outlive it goes to UiMemory in save()
 * and comes back in restore().
 */
class View
{
public:
    /** Constructor. */
    View() = default;

    /** Destructor. */
    virtual ~View() = default;

    /** Build the widget tree, once, right after construction. */
    virtual void build() = 0;

    /**
     * Get the screen this view was built as.
     *
     * @return the screen.
     */
    ViewId id() const
    {
        return id_;
    }

    /**
     * The list whose position UiMemory keeps for this screen.
     *
     * @return the list, or nullptr if the screen keeps none.
     */
    virtual List *list()
    {
        return nullptr;
    }

    /**
     * Store what must survive this view being destroyed.
     *
     * @param m: UI memory.
     */
    virtual void save(UiMemory &m) const
    {
        (void)m;
    }

    /**
     * Take back what save() stored, right after build() and before onShow().
     *
     * @param m: UI memory.
     */
    virtual void restore(UiMemory &m)
    {
        (void)m;
    }

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
    /**
     * Take the shared top bar for this screen: set its title, make it repaint
     * fully, and have the base syncFromState() refresh it. The view adds it to
     * its widget tree like any child.
     *
     * @param title: page title; empty shows the clock instead.
     * @return the top bar.
     */
    TopBar &useTopBar(const UiText &title);

    /**
     * Get the top bar shared by every screen.
     *
     * @return the top bar.
     */
    static TopBar &topBar();

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

private:
    friend class ViewSlot;

    bool hasTopBar_ = false;    ///< The screen shows the shared top bar
    bool syncRtx_ = false;
    ViewId id_ = ViewId::Count; ///< Screen this view was built as
};

} // namespace ortxui

#endif /* ORTX_UI_VIEW_HPP */
