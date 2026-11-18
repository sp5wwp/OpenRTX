/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/Navigator.hpp"
#include "core/Event.hpp"

namespace ortxui
{

void Navigator::setRoot(View *root)
{
    depth_ = 0;
    if (root != nullptr) {
        stack_[depth_++] = root;
        activate();
    }
}

void Navigator::push(View *v)
{
    if ((v == nullptr) || (depth_ >= kMaxDepth))
        return;

    stack_[depth_++] = v;
    activate();
}

void Navigator::pop()
{
    if (depth_ <= 1)
        return; /* the root view is never popped */

    depth_--;
    activate();
}

void Navigator::activate()
{
    View *v = active();
    if (v == nullptr)
        return;

    v->onShow();
    v->announce();
    v->screen().markAllDirty();
}

void Navigator::dispatch(const Event &e)
{
    View *v = active();
    if (v == nullptr)
        return;

    const NavIntent intent = v->onEvent(e);
    switch (intent.action) {
        case NavAction::Push:
            push(intent.target);
            break;
        case NavAction::Pop:
            pop();
            break;
        case NavAction::PopToRoot:
            popToRoot();
            break;
        case NavAction::None:
            break;
    }
}

void Navigator::popToRoot()
{
    if (depth_ <= 1)
        return;

    depth_ = 1;
    activate();
}

void Navigator::syncActive(const state_t &s)
{
    View *v = active();
    if (v != nullptr)
        v->syncFromState(s);
}

void Navigator::presentOverlay(View *v, OverlayDismiss policy, long long now,
                               uint32_t timeoutMs, bool (*cond)())
{
    if (v == nullptr)
        return;

    if (overlay_ == v) {
        /* Already showing: just refresh its policy and poke window so the
         * repeating trigger keeps the one overlay alive (no re-push). */
        overlayPolicy_ = policy;
        overlayTimeoutMs_ = timeoutMs;
        overlayCond_ = cond;
        overlayPoke_ = now;
        return;
    }

    /* A different managed overlay replaces whatever was up. */
    dismissOverlay();

    overlay_ = v;
    overlayPolicy_ = policy;
    overlayTimeoutMs_ = timeoutMs;
    overlayCond_ = cond;
    overlayPoke_ = now;
    push(v);
}

void Navigator::dismissOverlay()
{
    if (overlay_ == nullptr)
        return;

    /* Only unwind (and forget) our overlay while it is the exposed top of the
     * stack. If another modal was pushed above it, leave the bookkeeping in
     * place: tickOverlay() reclaims it once that modal pops and it is exposed
     * again, so a buried overlay is never orphaned. */
    if (active() != overlay_)
        return;

    pop();
    overlay_ = nullptr;
    overlayCond_ = nullptr;
}

void Navigator::tickOverlay(long long now)
{
    if (overlay_ == nullptr)
        return;

    /* Only act while our overlay is exposed; if a modal covers it, wait. */
    if (active() != overlay_)
        return;

    bool expired = false;
    if (overlayPolicy_ == OverlayDismiss::Transient)
        expired = (now - overlayPoke_) >= (long long)overlayTimeoutMs_;
    else if (overlayPolicy_ == OverlayDismiss::WhileActive)
        expired = (overlayCond_ == nullptr) || !overlayCond_();

    if (expired)
        dismissOverlay();
}

} // namespace ortxui
