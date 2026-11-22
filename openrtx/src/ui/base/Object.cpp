/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "base/Object.hpp"
#include "render/DrawCtx.hpp"

namespace ortxui
{

bool Object::onEvent(const Event &e)
{
    (void)e;
    return false;
}

void Object::addChild(Object *child)
{
    if (child == nullptr)
        return;

    child->parentLink = this;
    child->nextSiblingLink = nullptr;

    Object **link = &firstChildLink;
    while (*link != nullptr)
        link = &(*link)->nextSiblingLink;
    *link = child;
}

void Object::setArea(const Rect &r)
{
    if (r == bounds)
        return;

    bounds = r;
    invalidateScreen();
}

void Object::setFlag(ObjFlag f, bool on)
{
    const bool wasHidden = hasFlag(FlagHidden);

    if (on)
        objFlags |= f;
    else
        objFlags = static_cast<uint8_t>(objFlags & ~f);

    if (hasFlag(FlagHidden) != wasHidden)
        invalidateScreen();
}

void Object::invalidate()
{
    root()->onInvalidated(bounds);
}

void Object::invalidateScreen()
{
    root()->onInvalidated({ 0, 0, 0, 0 });
}

Object *Object::root()
{
    Object *node = this;
    while (node->parentLink != nullptr)
        node = node->parentLink;

    return node;
}

void Object::paintTree(DrawCtx &d)
{
    if (hasFlag(FlagHidden))
        return;

    /* Clip the subtree to its box; the root has no area and keeps the outer
     * clip. A box outside the clip, such as another band, draws nothing. */
    const bool clip = !bounds.empty();
    if (clip && intersect(d.clip(), bounds).empty())
        return;
    if (clip)
        d.pushClip(bounds);

    draw(d);
    for (Object *c = firstChildLink; c != nullptr; c = c->nextSiblingLink)
        c->paintTree(d);

    if (clip)
        d.popClip();
}

} // namespace ortxui
