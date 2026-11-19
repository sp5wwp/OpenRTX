/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "base/Navigator.hpp"
#include "base/Event.hpp"
#include "views/UiMemory.hpp"
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
    if ((id == ViewId::Count) || (depth_ >= MAX_DEPTH))
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
        old->save();
    }

    built_ = stack_[depth_ - 1];
    View *v = slot_.build(built_);

    List *l = v->list();
    if (l != nullptr)
        l->setPosition(mem_.lists[static_cast<uint8_t>(built_)]);
    v->restore();

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

void Navigator::presentOverlay(View *v, long long now, uint32_t timeoutMs)
{
    overlayShown_ = now;
    overlayTimeout_ = timeoutMs;
    if (overlay_ == v)
        return;

    dismissOverlay();
    overlay_ = v;
    activate(v, true);
}

void Navigator::dismissOverlay()
{
    if (overlay_ == nullptr)
        return;

    overlay_ = nullptr;

    /* The screen below stayed built and active: only repaint it. */
    slot_.view()->screen().markAllDirty();
}

void Navigator::tickOverlay(long long now)
{
    if ((overlay_ != nullptr) && ((now - overlayShown_) >= overlayTimeout_))
        dismissOverlay();
}

} // namespace ortxui
