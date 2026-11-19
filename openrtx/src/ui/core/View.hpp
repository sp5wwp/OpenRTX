/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_VIEW_HPP
#define ORTX_UI_VIEW_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

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
    None,      ///< Stay on this view
    Push,      ///< Open `target` as a child screen
    Pop,       ///< Return to the parent screen
    PopToRoot, ///< Unwind all the way to the home screen
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

    /** Called once the view is built and restored, before it is drawn. */
    virtual void onShow()
    {
    }

    /**
     * Announce this screen via voice prompts on entry, after onShow(). The
     * default speaks the top-bar title, then the selected row of list(); views
     * with richer content override it. Not called when the screen comes back
     * from the macro menu.
     */
    virtual void announce();

    /**
     * Update what the view shows from the state snapshot, before each frame.
     * The base refreshes the shared top bar; overrides call it first.
     *
     * @param s: state snapshot.
     */
    virtual void syncFromState(const state_t &s);

    /**
     * Handle an input event. The default goes back on ESC and passes anything
     * else to the Screen.
     *
     * @param e: event.
     * @return what the navigator does next.
     */
    virtual NavIntent onEvent(const Event &e);

    /** Get the view's widget-tree root. */
    virtual Screen &screen() = 0;

    /**
     * Take the view's request to reconfigure the radio from state.channel,
     * clearing it.
     *
     * @return true once after requestSyncRtx().
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

    /** Ask for the radio to be reconfigured from state.channel. */
    void requestSyncRtx()
    {
        syncRtx_ = true;
    }

    /**
     * Speak a text now, when voice prompts are on, such as a value changed in
     * place.
     *
     * @param text: text to speak; an entry plays its own prompt.
     */
    void vpSay(const UiText &text);

private:
    friend class ViewSlot;

    bool hasTopBar_ = false;    ///< The screen shows the shared top bar
    bool syncRtx_ = false;      ///< The radio is to be reconfigured
    ViewId id_ = ViewId::Count; ///< Screen this view was built as
};

} // namespace ortxui

#endif /* ORTX_UI_VIEW_HPP */
