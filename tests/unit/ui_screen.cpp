/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "base/Object.hpp"
#include "base/Screen.hpp"

using namespace ortxui;

/** An object with nothing to draw. */
class Box : public Object
{
public:
    /**
     * Paint nothing.
     *
     * @param d: drawing context.
     */
    void draw(DrawCtx &d) override
    {
        (void)d;
    }
};

static constexpr Rect UPPER = { 0, 10, 100, 20 }; ///< Rows 10 to 29
static constexpr Rect LOWER = { 0, 50, 100, 8 };  ///< Rows 50 to 57

/**
 * Check that a screen asks for a repaint of all its rows.
 *
 * @param screen: screen to check.
 * @return true if it does.
 */
static bool wholeScreenDirty(const Screen &screen)
{
    return screen.dirty() && (screen.dirtyTop() == 0)
        && (screen.dirtyBottom() == INT16_MAX);
}

TEST_CASE("A new screen repaints every row", "[ui][screen]")
{
    Screen screen;

    REQUIRE(wholeScreenDirty(screen));

    screen.clearDirty();
    REQUIRE_FALSE(screen.dirty());

    screen.markAllDirty();
    REQUIRE(wholeScreenDirty(screen));
}

TEST_CASE("An object that changes repaints its own rows", "[ui][screen]")
{
    Screen screen;
    Box upper;
    Box lower;
    screen.addChild(&upper);
    screen.addChild(&lower);
    upper.setArea(UPPER);
    lower.setArea(LOWER);
    screen.clearDirty();

    lower.invalidate();
    REQUIRE(screen.dirty());
    REQUIRE(screen.dirtyTop() == 50);
    REQUIRE(screen.dirtyBottom() == 58);

    /* A second object widens the rows to both. */
    upper.invalidate();
    REQUIRE(screen.dirtyTop() == 10);
    REQUIRE(screen.dirtyBottom() == 58);

    screen.clearDirty();
    REQUIRE_FALSE(screen.dirty());
}

TEST_CASE("An object deeper in the tree reaches the screen", "[ui][screen]")
{
    Screen screen;
    Box parent;
    Box child;
    screen.addChild(&parent);
    parent.addChild(&child);
    parent.setArea({ 0, 0, 100, 100 });
    child.setArea(LOWER);
    screen.clearDirty();

    child.invalidate();
    REQUIRE(screen.dirtyTop() == 50);
    REQUIRE(screen.dirtyBottom() == 58);
}

TEST_CASE("An object not placed yet repaints the whole screen", "[ui][screen]")
{
    Screen screen;
    Box box;
    screen.addChild(&box);
    screen.clearDirty();

    box.invalidate();
    REQUIRE(wholeScreenDirty(screen));
}

TEST_CASE("Moving or resizing an object repaints the whole screen",
          "[ui][screen]")
{
    Screen screen;
    Box box;
    screen.addChild(&box);
    box.setArea(UPPER);
    screen.clearDirty();

    /* The same area again changes nothing. */
    box.setArea(UPPER);
    REQUIRE_FALSE(screen.dirty());

    box.setArea(LOWER);
    REQUIRE(wholeScreenDirty(screen));

    screen.clearDirty();
    box.setArea({ 0, 50, 100, 16 });
    REQUIRE(wholeScreenDirty(screen));
}

TEST_CASE("Hiding or showing an object repaints the whole screen",
          "[ui][screen]")
{
    Screen screen;
    Box box;
    screen.addChild(&box);
    box.setArea(UPPER);
    screen.clearDirty();

    box.setFlag(FlagHidden, true);
    REQUIRE(wholeScreenDirty(screen));

    /* Hidden already: nothing changes. */
    screen.clearDirty();
    box.setFlag(FlagHidden, true);
    REQUIRE_FALSE(screen.dirty());

    box.setFlag(FlagHidden, false);
    REQUIRE(wholeScreenDirty(screen));

    /* The other flags move nothing. */
    screen.clearDirty();
    box.setFlag(FlagFocusable, true);
    REQUIRE_FALSE(screen.dirty());
}
