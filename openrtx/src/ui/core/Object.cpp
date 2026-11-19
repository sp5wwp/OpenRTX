/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/Object.hpp"
#include "core/Event.hpp"
#include "render/DrawCtx.hpp"
#include "core/input.h"
#include "core/event.h"
#include "interfaces/keyboard.h"
#include "hwconfig.h"

namespace ortxui
{

/* ---- Event decoding ---- */

Event Event::decode(uint8_t type, uint32_t payload)
{
    Event ev;

    if (type == EVENT_STATUS) {
        ev.kind = EvKind::Status;
        return ev;
    }

    /* EVENT_KBD: payload is a kbd_msg_t value. */
    kbd_msg_t msg;
    msg.value = payload;

    const uint32_t keys = msg.keys;
    if (keys & KNOB_LEFT) {
        ev.kind = EvKind::Encoder;
        ev.encoder = -1;
        return ev;
    }
    if (keys & KNOB_RIGHT) {
        ev.kind = EvKind::Encoder;
        ev.encoder = 1;
        return ev;
    }

    ev.kind = msg.long_press ? EvKind::KeyLong : EvKind::Key;
    ev.keys = keys;
    return ev;
}

/* ---- Object ---- */

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

/* ---- Screen ---- */

Screen::Screen()
{
    markAllDirty();
}

void Screen::draw(DrawCtx &d)
{
    (void)d; /* the root itself paints nothing; children do */
}

void Screen::onDescendantInvalidated(const Rect &area)
{
    (void)area; /* a repaint redraws the whole screen */
    hasDirty_ = true;
}

void Screen::markAllDirty()
{
    hasDirty_ = true;
}

void Screen::clearDirty()
{
    hasDirty_ = false;
}

void Screen::setFocus(Object *obj)
{
    if (focused_ == obj)
        return;

    if (focused_ != nullptr) {
        focused_->setFlag(FLAG_FOCUSED, false);
        focused_->invalidate();
    }

    focused_ = obj;

    if (focused_ != nullptr) {
        focused_->setFlag(FLAG_FOCUSED, true);
        focused_->invalidate();
    }
}

namespace
{

/*
 * Collect the focusable, non-hidden objects of a subtree in paint (pre-order)
 * order. Traversal is recursive so focusables nested inside layout containers
 * (Flex/Grid) are reached, not just the Screen's direct children.
 */
void collectFocusables(Object *node, Object **out, int &n, int max)
{
    for (Object *c = node->firstChild(); (c != nullptr) && (n < max);
         c = c->nextSibling()) {
        if (c->hasFlag(FLAG_HIDDEN))
            continue;
        if (c->hasFlag(FLAG_FOCUSABLE))
            out[n++] = c;
        collectFocusables(c, out, n, max);
    }
}

constexpr int kMaxFocusables = 32;

} // namespace

void Screen::focusFirst()
{
    Object *items[kMaxFocusables];
    int n = 0;
    collectFocusables(this, items, n, kMaxFocusables);
    if (n > 0)
        setFocus(items[0]);
}

void Screen::moveFocus(int dir)
{
    Object *items[kMaxFocusables];
    int n = 0;
    collectFocusables(this, items, n, kMaxFocusables);
    if (n == 0)
        return;

    int idx = 0;
    for (int i = 0; i < n; i++) {
        if (items[i] == focused_) {
            idx = i;
            break;
        }
    }

    idx = (idx + ((dir >= 0) ? 1 : -1) + n) % n;
    setFocus(items[idx]);
}

bool Screen::dispatch(const Event &e)
{
    if ((focused_ != nullptr) && focused_->onEvent(e))
        return true;

    /* Unconsumed knob steps move focus within the screen. */
    if (e.kind == EvKind::Encoder) {
        moveFocus(e.encoder);
        return true;
    }

    return false;
}

} // namespace ortxui
