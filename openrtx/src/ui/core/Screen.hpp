/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_SCREEN_HPP
#define ORTX_UI_SCREEN_HPP

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
    Screen();

    void draw(DrawCtx &d) override; //< root paints nothing itself

    /** Dispatch an event to the focused object; knob steps move focus. */
    bool dispatch(const Event &e);

    /* ---- Focus over focusable direct children ---- */
    void focusFirst();
    void moveFocus(int dir);
    Object *focused() const
    {
        return focused_;
    }

    /* ---- Repaint tracking ---- */
    bool dirty() const
    {
        return hasDirty_;
    }
    void markAllDirty();
    void clearDirty();

protected:
    void onDescendantInvalidated(const Rect &area) override;

private:
    void setFocus(Object *obj);

    Object *focused_ = nullptr;
    bool hasDirty_ = true; ///< The screen needs a repaint
};

} // namespace ortxui

#endif /* ORTX_UI_SCREEN_HPP */
