/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_OBJECT_HPP
#define ORTX_UI_OBJECT_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

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
    FLAG_HIDDEN = 1u << 0,    ///< Neither drawn nor laid out
    FLAG_FOCUSABLE = 1u << 1, ///< Takes part in the Screen focus ring
};

/**
 * Base class for every retained UI object.
 *
 * The tree is intrusive: each node stores pointers to its parent, first child
 * and next sibling, so building a screen allocates no containers. Areas
 * are in absolute screen coordinates. Objects are owned by their view, and none
 * is freed while it is in a tree.
 */
class Object
{
public:
    /** Constructor. */
    Object() : grow_(0), margin_(0), self_(0), flags_(0)
    {
    }

    /** Destructor. */
    virtual ~Object() = default;

    Object(const Object &) = delete;
    Object &operator=(const Object &) = delete;

    /**
     * Paint this object only (not its children) within its area.
     *
     * @param d: drawing context.
     */
    virtual void draw(DrawCtx &d) = 0;

    /**
     * Handle an input event.
     *
     * @param e: event.
     * @return true when consumed.
     */
    virtual bool onEvent(const Event &e);

    /**
     * Append a child, walking the siblings to the end: trees are built once
     * per screen, so no last-child pointer is kept for it.
     *
     * @param child: object to append.
     */
    void addChild(Object *child);

    /** Get the parent, nullptr for a root. */
    Object *parent() const
    {
        return parent_;
    }

    /** Get the first child, nullptr if none. */
    Object *firstChild() const
    {
        return firstChild_;
    }

    /** Get the next sibling, nullptr if none. */
    Object *nextSibling() const
    {
        return nextSibling_;
    }

    /**
     * Place the object.
     *
     * @param r: area in absolute screen coordinates.
     */
    void setArea(const Rect &r)
    {
        area_ = r;
    }

    /** Get the area, in absolute screen coordinates. */
    const Rect &area() const
    {
        return area_;
    }

    /**
     * Set or clear a flag.
     *
     * @param f: flag.
     * @param on: whether to set it.
     */
    void setFlag(ObjFlag f, bool on);

    /**
     * Test a flag.
     *
     * @param f: flag.
     * @return true if it is set.
     */
    bool hasFlag(ObjFlag f) const
    {
        return (flags_ & f) != 0u;
    }

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

    /**
     * Set the preferred main-axis extent.
     *
     * @param b: extent in pixels, -1 for natural().
     */
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

    /**
     * Set the cross-axis alignment of the child, overriding its parent's.
     *
     * @param a: alignment.
     */
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

    /** Mark this object as needing repaint, by telling the tree root. */
    void invalidate();

    /**
     * Recursively paint this subtree (pre-order, skipping hidden nodes).
     *
     * @param d: drawing context.
     */
    void paintTree(DrawCtx &d);

protected:
    /** Called on the tree root when an object in the tree invalidates. */
    virtual void onInvalidated()
    {
    }

    Object *parent_ = nullptr;      ///< Parent, nullptr for a root
    Object *firstChild_ = nullptr;  ///< First child
    Object *nextSibling_ = nullptr; ///< Next child of the parent
    Rect area_ = { 0, 0, 0, 0 };    ///< Area in screen coordinates

    /* Layout hints and flags, packed in a word: every widget carries them. */
    int16_t basis_ = -1; ///< Preferred main-axis extent, -1: natural()
    uint8_t grow_ : 4, margin_ : 4; ///< Flex weight; uniform margin (px)
    uint8_t self_ : 3, flags_ : 5;  ///< Cross alignment (Align); ObjFlag set
};

} // namespace ortxui

#endif /* ORTX_UI_OBJECT_HPP */
