/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "base/Screen.hpp"
#include "base/Event.hpp"
#include <algorithm>

namespace ortxui
{

/** Most focusable objects a screen can hold. */
static constexpr uint8_t MAX_FOCUSABLES = 32;

Screen::Screen()
{
    markAllDirty();
}

void Screen::draw(DrawCtx &d)
{
    (void)d;
}

void Screen::onInvalidated(const Rect &area)
{
    if (area.empty()) {
        markAllDirty();
        return;
    }

    dirtyBegin = std::min(dirtyBegin, area.y);
    dirtyEnd = std::max<int16_t>(dirtyEnd, area.bottom() + 1);
}

void Screen::markAllDirty()
{
    dirtyBegin = 0;
    dirtyEnd = INT16_MAX;
}

void Screen::clearDirty()
{
    dirtyBegin = INT16_MAX;
    dirtyEnd = 0;
}

void Screen::setFocus(Object *obj)
{
    if (focus == obj)
        return;

    if (focus != nullptr)
        focus->invalidate();

    focus = obj;

    if (focus != nullptr)
        focus->invalidate();
}

/**
 * Collect the focusable, non-hidden objects of a subtree in paint (pre-order)
 * order, descending into layout containers.
 *
 * @param node: subtree root.
 * @param out: collected objects.
 * @param n: number collected so far, updated.
 */
static void collectFocusables(Object *node, Object **out, uint8_t &n)
{
    for (Object *c = node->firstChild(); (c != nullptr) && (n < MAX_FOCUSABLES);
         c = c->nextSibling()) {
        if (c->hasFlag(FlagHidden))
            continue;
        if (c->hasFlag(FlagFocusable))
            out[n++] = c;
        collectFocusables(c, out, n);
    }
}

void Screen::focusFirst()
{
    Object *items[MAX_FOCUSABLES];
    uint8_t n = 0;
    collectFocusables(this, items, n);
    if (n > 0)
        setFocus(items[0]);
}

void Screen::moveFocus(int8_t dir)
{
    Object *items[MAX_FOCUSABLES];
    uint8_t n = 0;
    collectFocusables(this, items, n);
    if (n == 0)
        return;

    /* With nothing focused yet, the first step lands on either end. */
    uint8_t next = (dir >= 0) ? 0 : static_cast<uint8_t>(n - 1);
    for (uint8_t i = 0; i < n; i++) {
        if (items[i] == focus) {
            next = static_cast<uint8_t>((i + ((dir >= 0) ? 1 : n - 1)) % n);
            break;
        }
    }

    setFocus(items[next]);
}

bool Screen::dispatch(const Event &e)
{
    if ((focus != nullptr) && focus->onEvent(e))
        return true;

    if (e.kind == EvKind::Encoder) {
        moveFocus(e.encoder);
        return true;
    }

    return false;
}

} // namespace ortxui
