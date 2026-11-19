/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_NAVIGATOR_HPP
#define ORTX_UI_NAVIGATOR_HPP

#include <cstdint>
#include "core/View.hpp"
#include "core/ViewId.hpp"

namespace ortxui
{

struct Event;
struct UiMemory;
class ViewSlot;

/**
 * How a managed overlay is auto-dismissed by tickOverlay().
 */
enum class OverlayDismiss : uint8_t {
    Transient,   //< auto-hide after timeoutMs with no re-poke (volume HUD, ...)
    WhileActive, //< visible only while cond() holds (TX/RX detail overlays, ...)
};

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

    /** Forward the state snapshot to the active view. */
    void syncActive(const state_t &s);

    /**
     * Get the active view: the managed overlay when one is shown, else the
     * built screen.
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

    /** Close the modal screen and rebuild the one below it. */
    void closeOverlay()
    {
        pop();
    }

    /**
     * Present `v` as a *managed* overlay above the current screen and drive its
     * dismissal centrally from tickOverlay(). The view below stays built. A
     * managed overlay auto-hides:
     *   Transient   - after `timeoutMs` since the last pokeOverlay(now);
     *   WhileActive - as soon as `cond()` returns false.
     * Re-presenting the same view just refreshes its policy/poke, so a
     * repeating trigger (e.g. the volume knob turning) keeps one overlay alive.
     * One managed overlay at a time; a different one replaces it. This is the
     * shared layer behind the volume HUD and, later, the TX/RX detail views.
     */
    void presentOverlay(View *v, OverlayDismiss policy, long long now,
                        uint32_t timeoutMs = 0, bool (*cond)() = nullptr);

    /** Refresh a Transient overlay's timeout window (call on each re-trigger). */
    void pokeOverlay(long long now)
    {
        overlayPoke_ = now;
    }

    /** Apply the managed overlay's dismiss policy; call once per UI tick. */
    void tickOverlay(long long now);

    /** Dismiss the managed overlay now, if one is shown. */
    void dismissOverlay();

    /** The managed overlay currently shown, or nullptr. */
    View *overlay() const
    {
        return overlay_;
    }

private:
    static constexpr uint8_t kMaxDepth = 8;

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

    /** Save and destroy the built view, then build the top of the stack. */
    void rebuild();

    /**
     * Run a view's entry hooks and mark it for a full repaint.
     *
     * @param v: view becoming active.
     */
    static void activate(View *v);

    ViewSlot &slot_;               ///< Where the top screen is built
    UiMemory &mem_;                ///< What survives a view
    ViewId stack_[kMaxDepth];      ///< Screens, root first
    ViewId built_ = ViewId::Count; ///< Screen currently built in the slot
    uint8_t depth_ = 0;            ///< Number of screens on the stack

    /* Managed-overlay bookkeeping (see presentOverlay). */
    View *overlay_ = nullptr;
    OverlayDismiss overlayPolicy_ = OverlayDismiss::Transient;
    uint32_t overlayTimeoutMs_ = 0;
    long long overlayPoke_ = 0;
    bool (*overlayCond_)() = nullptr;
};

} // namespace ortxui

#endif /* ORTX_UI_NAVIGATOR_HPP */
