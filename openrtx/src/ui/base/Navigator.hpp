/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_NAVIGATOR_HPP
#define ORTX_UI_NAVIGATOR_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <array>
#include <cstdint>
#include "base/View.hpp"
#include "base/ViewId.hpp"

namespace ortxui
{

struct Event;
struct UiMemory;
class ViewSlot;

/**
 * A depth-bounded stack of screens.
 *
 * ENTER-style actions push a child screen, ESC pops back to the parent, and the
 * top of the stack is the active screen that ui_shim draws and feeds state to.
 * The stack holds ViewIds and only the top one is built, in a ViewSlot: going
 * to another screen saves the current view into UiMemory, destroys it and
 * builds the new one, which restores its own state from UiMemory. Activating a
 * view marks it fully dirty so it repaints cleanly over whatever screen it now
 * covers. The root screen is never popped, so ESC on the home screen is a
 * no-op.
 */
class Navigator
{
public:
    /**
     * Constructor.
     *
     * @param slot: where the active view is built.
     * @param mem: what survives a view being destroyed.
     */
    Navigator(ViewSlot &slot, UiMemory &mem);

    /** Destructor. */
    ~Navigator();

    /**
     * Clear the stack and make a screen the (unpoppable) base screen.
     *
     * @param root: home screen.
     */
    void setRoot(ViewId root);

    /**
     * Feed an event to the active view and apply its navigation intent.
     *
     * @param e: event.
     * @return true if the view asked to reconfigure the radio.
     */
    bool dispatch(const Event &e);

    /**
     * Get the active view: the overlay when one is shown, else the built
     * screen.
     *
     * @return the view, or nullptr before setRoot().
     */
    View *active() const;

    /**
     * Get the screen at the top of the stack.
     *
     * @return the screen, or ViewId::Count before setRoot().
     */
    ViewId activeId() const
    {
        return (depth_ > 0) ? stack_[depth_ - 1] : ViewId::Count;
    }

    /**
     * Open a modal screen (e.g. the macro menu) on top of the current one. It
     * replaces the current view until closeOverlay() rebuilds it. Public so
     * ui_shim can open a screen triggered globally (a held key), outside a
     * view's onEvent intent.
     *
     * @param id: screen to open.
     */
    void openOverlay(ViewId id)
    {
        push(id);
    }

    /**
     * Close the modal screen and rebuild the one below it, without announcing
     * it again: it is the screen the user was already on.
     */
    void closeOverlay();

    /**
     * Show a view over the current screen, which stays built, until
     * `timeoutMs` pass without it being presented again. Presenting it again
     * restarts the timeout, so a repeating trigger keeps it up.
     *
     * @param v: view to show.
     * @param now: current tick.
     * @param timeoutMs: time it stays up.
     */
    void presentOverlay(View *v, long long now, uint32_t timeoutMs);

    /**
     * Dismiss the overlay once its timeout has passed; call once per UI tick.
     *
     * @param now: current tick.
     */
    void tickOverlay(long long now);

    /** Dismiss the overlay now, if one is shown. */
    void dismissOverlay();

    /**
     * Get the overlay shown.
     *
     * @return the overlay, or nullptr.
     */
    View *overlay() const
    {
        return overlay_;
    }

private:
    /**
     * Push a screen onto the stack and build it.
     *
     * @param id: screen to push.
     */
    void push(ViewId id);

    /** Pop the top screen and rebuild the one below it. */
    void pop();

    /** Pop every screen but the root and rebuild it. */
    void popToRoot();

    /**
     * Save and destroy the built view, then build the top of the stack.
     *
     * @param announce: whether the new view announces itself.
     */
    void rebuild(bool announce = true);

    /**
     * Run a view's entry hooks and mark it for a full repaint.
     *
     * @param v: view becoming active.
     * @param announce: whether the view announces itself.
     */
    static void activate(View *v, bool announce);

    /** Deepest stack of screens. */
    static constexpr uint8_t MAX_DEPTH = 8;

    ViewSlot &slot_;                      ///< Where the top screen is built
    UiMemory &mem_;                       ///< What survives a view
    View *overlay_ = nullptr;             ///< Overlay shown, or nullptr
    long long overlayShown_ = 0;          ///< Tick it was last presented
    uint32_t overlayTimeout_ = 0;         ///< Time it stays up, in ms
    std::array<ViewId, MAX_DEPTH> stack_; ///< Screens, root first
    ViewId built_ = ViewId::Count;        ///< Screen built in the slot
    uint8_t depth_ = 0;                   ///< Number of screens on the stack
};

} // namespace ortxui

#endif /* ORTX_UI_NAVIGATOR_HPP */
