/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_SCREEN_HPP
#define ORTX_UI_SCREEN_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include "base/Object.hpp"

namespace ortxui
{

/**
 * Root of a view's widget tree: owns the focus and the rows to repaint, and
 * sends events to the focused object. A repaint draws the rows of the objects
 * that invalidated since the last one; an object that moved, hid or showed
 * invalidates the whole screen.
 */
class Screen : public Object
{
public:
    /** Constructor. */
    Screen();

    /** Destructor. */
    ~Screen() = default;

    /**
     * Paint nothing: the root has no content of its own.
     *
     * @param d: drawing context.
     */
    void draw(DrawCtx &d) override;

    /**
     * Dispatch an event to the focused object; unconsumed knob steps move the
     * focus.
     *
     * @param e: event.
     * @return true when consumed.
     */
    bool dispatch(const Event &e);

    /** Focus the first focusable object. */
    void focusFirst();

    /**
     * Move the focus to the next or previous focusable object, wrapping.
     *
     * @param dir: +1 next, -1 previous.
     */
    void moveFocus(int8_t dir);

    /** Get the focused object, nullptr if none. */
    Object *focused() const
    {
        return focus;
    }

    /**
     * Check whether the screen needs a repaint.
     *
     * @return true if it does.
     */
    bool dirty() const
    {
        return dirtyEnd > dirtyBegin;
    }

    /** Get the first row needing a repaint. */
    int16_t dirtyTop() const
    {
        return dirtyBegin;
    }

    /** Get the row after the last one needing a repaint. */
    int16_t dirtyBottom() const
    {
        return dirtyEnd;
    }

    /** Ask for a repaint. */
    void markAllDirty();

    /** Record that the screen was repainted. */
    void clearDirty();

protected:
    /**
     * Ask for a repaint of an area's rows when an object invalidates.
     *
     * @param area: the area to repaint; an empty one is the whole screen.
     */
    void onInvalidated(const Rect &area) override;

private:
    /**
     * Move the focus to an object.
     *
     * @param obj: object to focus.
     */
    void setFocus(Object *obj);

    Object *focus = nullptr;      ///< Focused object, nullptr if none
    int16_t dirtyBegin = 0;       ///< First row needing a repaint
    int16_t dirtyEnd = INT16_MAX; ///< Row after the last one, 0 if none
};

} // namespace ortxui

#endif /* ORTX_UI_SCREEN_HPP */
