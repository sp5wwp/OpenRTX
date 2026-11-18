/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_OBJECT_HPP
#define ORTX_UI_OBJECT_HPP

#include <cstdint>
#include "core/Geometry.hpp"
#include "core/Layout.hpp"

namespace ortxui
{

class DrawCtx;
struct Event;

/**
 * Per-object state flags (bitmask).
 */
enum ObjFlag : uint8_t {
    FLAG_HIDDEN = 1u << 0,
    FLAG_FOCUSABLE = 1u << 1,
    FLAG_FOCUSED = 1u << 2,
    FLAG_EDITING = 1u << 3,
    FLAG_DIRTY = 1u << 4,
};

/**
 * Base class for every retained UI object.
 *
 * The tree is intrusive: each node stores pointers to its parent, first/last
 * child and next sibling, so building a screen allocates no containers. Areas
 * are in absolute screen coordinates. Objects live in static storage owned by
 * their view (placement-new / direct members), never on the heap.
 */
class Object
{
public:
    Object() = default;
    virtual ~Object() = default;

    Object(const Object &) = delete;
    Object &operator=(const Object &) = delete;

    /** Paint this object only (not its children) within its area. */
    virtual void draw(DrawCtx &d) = 0;

    /** Handle an input event; return true when consumed. */
    virtual bool onEvent(const Event &e);

    /* ---- Tree ---- */
    void addChild(Object *child);
    Object *parent() const
    {
        return parent_;
    }
    Object *firstChild() const
    {
        return firstChild_;
    }
    Object *nextSibling() const
    {
        return nextSibling_;
    }

    /* ---- Geometry (absolute screen coordinates) ---- */
    void setArea(const Rect &r)
    {
        area_ = r;
    }
    const Rect &area() const
    {
        return area_;
    }

    /* ---- Flags ---- */
    void setFlag(ObjFlag f, bool on);
    bool hasFlag(ObjFlag f) const
    {
        return (flags_ & f) != 0u;
    }

    /* ---- Layout ---- */

    /** Layout hints read by a Flex/Grid parent when placing this child. */
    LayoutHints &layout()
    {
        return layout_;
    }
    const LayoutHints &layout() const
    {
        return layout_;
    }
    void setGrow(uint8_t g)
    {
        layout_.grow = g;
    }
    void setBasis(int16_t b)
    {
        layout_.basis = b;
    }
    void setMargin(uint8_t m)
    {
        layout_.margin = m;
    }
    void setAlignSelf(Align a)
    {
        layout_.self = a;
    }

    /**
     * Intrinsic preferred size. The default is the current area size; widgets
     * whose size depends on content (e.g. measured text) override this so a
     * layout container can size them without an explicit basis.
     */
    virtual Size natural() const
    {
        return { area_.w, area_.h };
    }

    /**
     * Arrange descendants after this object's own area has been set. Leaf
     * objects do nothing; layout containers override this to position their
     * children (in absolute coordinates) and recurse.
     */
    virtual void onLayout()
    {
    }

    /** Mark this object as needing repaint, propagating to the tree root. */
    void invalidate();

    /** Recursively paint this subtree (pre-order, skipping hidden nodes). */
    void paintTree(DrawCtx &d);

protected:
    /**
     * Called on the tree root when a descendant invalidates. The Screen root
     * overrides this to accumulate a dirty rectangle.
     */
    virtual void onDescendantInvalidated(const Rect &area);

    Object *parent_ = nullptr;
    Object *firstChild_ = nullptr;
    Object *lastChild_ = nullptr;
    Object *nextSibling_ = nullptr;
    Rect area_ = { 0, 0, 0, 0 };
    uint8_t flags_ = 0;
    LayoutHints layout_ = {};
};

/**
 * The root of a screen's widget tree. Owns focus and the per-frame dirty
 * rectangle, and dispatches events to the focused object with a rotary-knob
 * focus-ring fallback. Concrete screens are expressed as Views that populate a
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

    /* ---- Dirty-rectangle tracking ---- */
    bool dirty() const
    {
        return hasDirty_;
    }
    const Rect &dirtyArea() const
    {
        return dirty_;
    }
    void markAllDirty();
    void clearDirty();

protected:
    void onDescendantInvalidated(const Rect &area) override;

private:
    void setFocus(Object *obj);

    Object *focused_ = nullptr;
    Rect dirty_ = { 0, 0, 0, 0 };
    bool hasDirty_ = true;
};

} // namespace ortxui

#endif /* ORTX_UI_OBJECT_HPP */
