/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/Navigator.hpp"
#include "core/Event.hpp"
#include "core/UiMemory.hpp"
#include "views/ViewSlot.hpp"
#include "widgets/List.hpp"

namespace ortxui
{

Navigator::Navigator(ViewSlot &slot, UiMemory &mem) :
    slot_(slot), mem_(mem), stack_()
{
}

Navigator::~Navigator()
{
}

void Navigator::setRoot(ViewId root)
{
    overlay_ = nullptr;
    overlayCond_ = nullptr;
    stack_[0] = root;
    depth_ = 1;
    rebuild();
}

View *Navigator::active() const
{
    return (overlay_ != nullptr) ? overlay_ : slot_.view();
}

void Navigator::push(ViewId id)
{
    if ((id == ViewId::Count) || (depth_ >= kMaxDepth))
        return;

    stack_[depth_++] = id;
    rebuild();
}

void Navigator::pop()
{
    if (depth_ <= 1)
        return; /* the root view is never popped */

    depth_--;
    rebuild();
}

void Navigator::popToRoot()
{
    if (depth_ <= 1)
        return;

    depth_ = 1;
    rebuild();
}

void Navigator::closeOverlay()
{
    if (depth_ <= 1)
        return;

    depth_--;
    rebuild(false);
}

void Navigator::rebuild(bool announce)
{
    View *old = slot_.view();
    if (old != nullptr) {
        const List *l = old->list();
        if (l != nullptr)
            mem_.lists[static_cast<uint8_t>(built_)] = l->position();
        old->save(mem_);
    }

    built_ = stack_[depth_ - 1];
    View *v = slot_.build(built_);

    List *l = v->list();
    if (l != nullptr)
        l->setPosition(mem_.lists[static_cast<uint8_t>(built_)]);
    v->restore(mem_);

    activate(v, announce);
}

void Navigator::activate(View *v, bool announce)
{
    v->onShow();
    if (announce)
        v->announce();
    v->screen().markAllDirty();
}

bool Navigator::dispatch(const Event &e)
{
    View *v = active();
    if (v == nullptr)
        return false;

    /* Take the resync request before navigating: a push or pop destroys the
     * view that made it. */
    const NavIntent intent = v->onEvent(e);
    const bool sync = v->takeSyncRtx();

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

    return sync;
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
         * repeating trigger keeps the one overlay alive. */
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
    activate(v, true);
}

void Navigator::dismissOverlay()
{
    if (overlay_ == nullptr)
        return;

    overlay_ = nullptr;
    overlayCond_ = nullptr;

    /* The screen below stayed built and active: only repaint it. */
    slot_.view()->screen().markAllDirty();
}

void Navigator::tickOverlay(long long now)
{
    if (overlay_ == nullptr)
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
