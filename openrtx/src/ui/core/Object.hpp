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
 * The tree is intrusive: each node stores pointers to its parent, first child
 * and next sibling, so building a screen allocates no containers. Areas
 * are in absolute screen coordinates. Objects live in static storage owned by
 * their view (placement-new / direct members), never on the heap.
 */
class Object
{
public:
    /** Constructor. */
    Object() : grow_(0), margin_(0), self_(0), flags_(0)
    {
    }

    virtual ~Object() = default;

    Object(const Object &) = delete;
    Object &operator=(const Object &) = delete;

    /** Paint this object only (not its children) within its area. */
    virtual void draw(DrawCtx &d) = 0;

    /** Handle an input event; return true when consumed. */
    virtual bool onEvent(const Event &e);

    /* ---- Tree ---- */

    /**
     * Append a child, walking the siblings to the end: trees are built once
     * per screen, so no last-child pointer is kept for it.
     *
     * @param child: object to append.
     */
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

    /**
     * Get the layout hints a Flex parent reads when placing this child.
     *
     * @return the hints.
     */
    LayoutHints layout() const
    {
        return { grow_, basis_, margin_, static_cast<Align>(self_) };
    }

    /**
     * Set the main-axis flex weight.
     *
     * @param g: weight, 0 (fixed) to 15.
     */
    void setGrow(uint8_t g)
    {
        grow_ = g;
    }
    void setBasis(int16_t b)
    {
        basis_ = b;
    }

    /**
     * Set the uniform margin around the child.
     *
     * @param m: margin in pixels, 0 to 15.
     */
    void setMargin(uint8_t m)
    {
        margin_ = m;
    }
    void setAlignSelf(Align a)
    {
        self_ = static_cast<uint8_t>(a);
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
    Object *nextSibling_ = nullptr;
    Rect area_ = { 0, 0, 0, 0 };

    /* Layout hints and flags, packed in a word: every widget carries them. */
    int16_t basis_ = -1; ///< Preferred main-axis extent, -1: natural()
    uint8_t grow_ : 4, margin_ : 4; ///< Flex weight; uniform margin (px)
    uint8_t self_ : 3, flags_ : 5;  ///< Cross alignment (Align); ObjFlag set
};

} // namespace ortxui

#endif /* ORTX_UI_OBJECT_HPP */
