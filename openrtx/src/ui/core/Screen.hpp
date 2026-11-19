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

#include "core/Object.hpp"

namespace ortxui
{

/**
 * The root of a screen's widget tree. Owns focus and whether the screen needs
 * a repaint, and dispatches events to the focused object with a rotary-knob
 * focus-ring fallback. A repaint redraws the whole screen, so no dirty
 * rectangle is kept. Concrete screens are expressed as Views that populate a
 * Screen; see views/.
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
        return focused_;
    }

    /** Check whether the screen needs a repaint. */
    bool dirty() const
    {
        return dirty_;
    }

    /** Ask for a repaint. */
    void markAllDirty();

    /** Record that the screen was repainted. */
    void clearDirty();

protected:
    /** Ask for a repaint when an object in the tree invalidates. */
    void onInvalidated() override;

private:
    /**
     * Move the focus to an object.
     *
     * @param obj: object to focus.
     */
    void setFocus(Object *obj);

    Object *focused_ = nullptr; ///< Focused object, nullptr if none
    bool dirty_ = true;         ///< The screen needs a repaint
};

} // namespace ortxui

#endif /* ORTX_UI_SCREEN_HPP */
