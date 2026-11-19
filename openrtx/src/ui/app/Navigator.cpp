/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "app/Navigator.hpp"
#include "base/Event.hpp"
#include "views/UiMemory.hpp"
#include "app/ViewSlot.hpp"
#include "widgets/List.hpp"

namespace ortxui
{

Navigator::Navigator(ViewSlot &slot, UiMemory &mem) :
    viewSlot(slot), memory(mem), stack()
{
}

Navigator::~Navigator()
{
}

void Navigator::setRoot(ViewId root)
{
    overlayView = nullptr;
    stack[0] = root;
    depth = 1;
    rebuild();
}

View *Navigator::active() const
{
    return (overlayView != nullptr) ? overlayView : viewSlot.view();
}

void Navigator::push(ViewId id)
{
    if ((id == ViewId::Count) || (depth >= MAX_DEPTH))
        return;

    stack[depth++] = id;
    rebuild();
}

void Navigator::pop()
{
    if (depth <= 1)
        return;

    depth--;
    rebuild();
}

void Navigator::popToRoot()
{
    if (depth <= 1)
        return;

    depth = 1;
    rebuild();
}

void Navigator::closeModal()
{
    if (depth <= 1)
        return;

    depth--;
    rebuild(false);
}

void Navigator::rebuild(bool announce)
{
    View *old = viewSlot.view();
    if (old != nullptr) {
        const List *l = old->list();
        if (l != nullptr)
            memory.lists[static_cast<uint8_t>(built)] = l->position();
        old->save();
    }

    built = stack[depth - 1];
    View *v = viewSlot.build(built);

    List *l = v->list();
    if (l != nullptr)
        l->setPosition(memory.lists[static_cast<uint8_t>(built)]);
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
    overlayShown = now;
    overlayTimeout = timeoutMs;
    if (overlayView == v)
        return;

    dismissOverlay();
    overlayView = v;
    activate(v, true);
}

void Navigator::dismissOverlay()
{
    if (overlayView == nullptr)
        return;

    overlayView = nullptr;

    /* The screen below stayed built and active: only repaint it. */
    viewSlot.view()->screen().markAllDirty();
}

void Navigator::tickOverlay(long long now)
{
    if ((overlayView != nullptr) && ((now - overlayShown) >= overlayTimeout))
        dismissOverlay();
}

} // namespace ortxui
