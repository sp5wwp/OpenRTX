/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/Object.hpp"
#include "render/DrawCtx.hpp"

namespace ortxui
{

bool Object::onEvent(const Event &e)
{
    (void)e;
    return false;
}

void Object::onDescendantInvalidated(const Rect &area)
{
    /* Non-root objects forward toward the root. */
    if (parent_ != nullptr)
        parent_->onDescendantInvalidated(area);
}

void Object::addChild(Object *child)
{
    if (child == nullptr)
        return;

    child->parent_ = this;
    child->nextSibling_ = nullptr;

    Object **link = &firstChild_;
    while (*link != nullptr)
        link = &(*link)->nextSibling_;
    *link = child;
}

void Object::setFlag(ObjFlag f, bool on)
{
    if (on)
        flags_ |= f;
    else
        flags_ = static_cast<uint8_t>(flags_ & ~f);
}

void Object::invalidate()
{
    flags_ |= FLAG_DIRTY;

    Object *root = this;
    while (root->parent_ != nullptr)
        root = root->parent_;

    root->onDescendantInvalidated(area_);
}

void Object::paintTree(DrawCtx &d)
{
    if (hasFlag(FLAG_HIDDEN))
        return;

    /* Clip this object (and its subtree) to its box, so content never bleeds
     * past its bounds. The root Screen has no area set, so it inherits the
     * full-screen clip. A box wholly outside the clip, such as outside the
     * band being drawn, draws nothing: skip the subtree. */
    const bool clip = !area_.empty();
    if (clip && intersect(d.clip(), area_).empty())
        return;
    if (clip)
        d.pushClip(area_);

    draw(d);
    for (Object *c = firstChild_; c != nullptr; c = c->nextSibling_)
        c->paintTree(d);

    if (clip)
        d.popClip();
}

} // namespace ortxui
