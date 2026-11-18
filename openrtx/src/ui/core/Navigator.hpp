/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_NAVIGATOR_HPP
#define ORTX_UI_NAVIGATOR_HPP

#include <cstdint>
#include "core/View.hpp"

namespace ortxui
{

struct Event;

/**
 * How a managed overlay is auto-dismissed by tickOverlay().
 */
enum class OverlayDismiss : uint8_t {
    Transient,   //< auto-hide after timeoutMs with no re-poke (volume HUD, ...)
    WhileActive, //< visible only while cond() holds (TX/RX detail overlays, ...)
};

/**
 * A depth-bounded stack of Views.
 *
 * ENTER-style actions push a child view, ESC pops back to the parent, and the
 * top of the stack is the active screen that ui_shim draws and feeds state to.
 * Activating a view marks it fully dirty so it repaints cleanly over whatever
 * screen it now covers. The root view is never popped, so ESC on the home
 * screen is a no-op. All storage is static (no heap): the stack is a small
 * fixed array of View pointers.
 */
class Navigator
{
public:
    /** Clear the stack and make `root` the (unpoppable) base screen. */
    void setRoot(View *root);

    /** Feed an event to the active view and apply its navigation intent. */
    void dispatch(const Event &e);

    /** Forward the state snapshot to the active view. */
    void syncActive(const state_t &s);

    /** The current top-of-stack view, or nullptr before setRoot(). */
    View *active() const
    {
        return (depth_ > 0) ? stack_[depth_ - 1] : nullptr;
    }

    /**
     * Open/close a modal overlay (e.g. the macro menu) on top of the current
     * screen. Unlike push/pop these are public so ui_shim can drive an overlay
     * that is triggered globally (a held key), outside a view's onEvent intent.
     */
    void openOverlay(View *v)
    {
        push(v);
    }
    void closeOverlay()
    {
        pop();
    }

    /**
     * Present `v` as a *managed* overlay above the current screen and drive its
     * dismissal centrally from tickOverlay(). Unlike openOverlay/closeOverlay
     * (a held-key modal the caller pops itself), a managed overlay auto-hides:
     *   Transient   - after `timeoutMs` since the last pokeOverlay(now);
     *   WhileActive - as soon as `cond()` returns false.
     * Re-presenting the same view just refreshes its policy/poke (no re-push),
     * so a repeating trigger (e.g. the volume knob turning) keeps one overlay
     * alive. One managed overlay at a time; a different one replaces it. This is
     * the shared layer behind the volume HUD and, later, the TX/RX detail views.
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

    /** Dismiss the managed overlay now, if it is the exposed top of stack. */
    void dismissOverlay();

    /** The managed overlay currently shown, or nullptr. */
    View *overlay() const
    {
        return overlay_;
    }

private:
    static constexpr uint8_t kMaxDepth = 8;

    void push(View *v);
    void pop();
    void popToRoot();
    void activate();

    View *stack_[kMaxDepth] = {};
    uint8_t depth_ = 0;

    /* Managed-overlay bookkeeping (see presentOverlay). */
    View *overlay_ = nullptr;
    OverlayDismiss overlayPolicy_ = OverlayDismiss::Transient;
    uint32_t overlayTimeoutMs_ = 0;
    long long overlayPoke_ = 0;
    bool (*overlayCond_)() = nullptr;
};

} // namespace ortxui

#endif /* ORTX_UI_NAVIGATOR_HPP */
