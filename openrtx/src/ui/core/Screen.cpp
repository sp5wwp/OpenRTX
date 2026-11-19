/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/Screen.hpp"
#include "core/Event.hpp"

namespace ortxui
{

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

/**
 * Collect the focusable, non-hidden objects of a subtree in paint (pre-order)
 * order. Traversal is recursive so focusables nested inside layout containers
 * (Flex/Grid) are reached, not just the Screen's direct children.
 *
 * @param node: subtree root.
 * @param out: collected objects.
 * @param n: number collected so far, updated.
 * @param max: capacity of out.
 */
static void collectFocusables(Object *node, Object **out, int &n, int max)
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

static constexpr int kMaxFocusables = 32;

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
