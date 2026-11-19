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

void Object::setFlag(ObjFlag f, bool on)
{
    if (on)
        objFlags |= f;
    else
        objFlags = static_cast<uint8_t>(objFlags & ~f);
}

void Object::invalidate()
{
    Object *root = this;
    while (root->parentLink != nullptr)
        root = root->parentLink;

    root->onInvalidated();
}

void Object::paintTree(DrawCtx &d)
{
    if (hasFlag(FlagHidden))
        return;

    /* Clip this object (and its subtree) to its box, so content never bleeds
     * past its bounds. The root Screen has no area set, so it inherits the
     * full-screen clip. A box wholly outside the clip, such as outside the
     * band being drawn, draws nothing: skip the subtree. */
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
