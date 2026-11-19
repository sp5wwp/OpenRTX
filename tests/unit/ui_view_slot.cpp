/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>

#include "base/Event.hpp"
#include "base/Navigator.hpp"
#include "base/UiMemory.hpp"
#include "core/state.h"
#include "interfaces/keyboard.h"
#include "views/ViewSlot.hpp"

using namespace ortxui;

/**
 * Make a key press event.
 *
 * @param keys: keyboard bitmask.
 * @return the event.
 */
static Event key(uint32_t keys)
{
    Event e;
    e.kind = EvKind::Key;
    e.keys = keys;
    return e;
}

/**
 * Make a long key press event.
 *
 * @param keys: keyboard bitmask.
 * @return the event.
 */
static Event longKey(uint32_t keys)
{
    Event e = key(keys);
    e.kind = EvKind::KeyLong;
    return e;
}

/**
 * Get the selected row of the active view's remembered list.
 *
 * @param nav: navigator.
 * @return the selected row.
 */
static uint16_t selectedRow(const Navigator &nav)
{
    return nav.active()->list()->selected();
}

/**
 * Forget every list position, so a test does not see an earlier one's.
 */
static void forgetLists()
{
    uiMemory.lists.fill({ 0, 0 });
}

/**
 * An empty view to present as an overlay.
 */
class ProbeOverlay : public View
{
public:
    /** Build nothing. */
    void build() override
    {
    }

    /** The view's widget-tree root. */
    Screen &screen() override
    {
        return screen_;
    }

private:
    Screen screen_; ///< Widget-tree root
};

TEST_CASE("Every screen builds and is destroyed", "[ui][slot]")
{
    ViewSlot slot;

    for (uint8_t i = 0; i < VIEW_COUNT; i++) {
        const ViewId id = static_cast<ViewId>(i);
        INFO("screen " << static_cast<int>(i));

        View *v = slot.build(id);
        REQUIRE(slot.view() == v);
        REQUIRE(v->id() == id);
        v->screen().markAllDirty();
        REQUIRE(v->screen().dirty());
    }

    slot.destroy();
    REQUIRE(slot.view() == nullptr);
}

TEST_CASE("Push and pop rebuild the screens below", "[ui][slot]")
{
    ViewSlot slot;
    Navigator nav(slot, uiMemory);
    forgetLists();

    nav.setRoot(ViewId::Vfo);
    REQUIRE(nav.activeId() == ViewId::Vfo);
    REQUIRE(nav.active()->id() == ViewId::Vfo);

    SECTION("a parent comes back on the row it was left on")
    {
        nav.dispatch(key(KEY_ENTER));
        REQUIRE(nav.active()->id() == ViewId::MainMenu);

        nav.dispatch(key(KEY_DOWN));
        nav.dispatch(key(KEY_DOWN));
        REQUIRE(selectedRow(nav) == 2);

        nav.dispatch(key(KEY_ENTER));
        REQUIRE(nav.activeId() == ViewId::Contacts);
        REQUIRE(nav.active()->id() == ViewId::Contacts);

        nav.dispatch(key(KEY_ESC));
        REQUIRE(nav.active()->id() == ViewId::MainMenu);
        REQUIRE(selectedRow(nav) == 2);

        /* The position outlives the menu being popped. */
        nav.dispatch(key(KEY_ESC));
        REQUIRE(nav.active()->id() == ViewId::Vfo);
        nav.dispatch(key(KEY_ENTER));
        REQUIRE(nav.active()->id() == ViewId::MainMenu);
        REQUIRE(selectedRow(nav) == 2);
    }

    SECTION("pop to root rebuilds the home screen")
    {
        nav.dispatch(key(KEY_ENTER));
        nav.dispatch(key(KEY_ENTER)); /* row 0: banks */
        REQUIRE(nav.active()->id() == ViewId::Banks);

        nav.dispatch(key(KEY_ENTER)); /* all channels */
        REQUIRE(nav.activeId() == ViewId::Vfo);
        REQUIRE(nav.active()->id() == ViewId::Vfo);
    }

    SECTION("the root is never popped")
    {
        nav.dispatch(key(KEY_ESC));
        REQUIRE(nav.activeId() == ViewId::Vfo);
    }
}

TEST_CASE("The macro menu replaces and restores the screen below", "[ui][slot]")
{
    ViewSlot slot;
    Navigator nav(slot, uiMemory);
    forgetLists();

    nav.setRoot(ViewId::Vfo);
    nav.openOverlay(ViewId::Radio);
    REQUIRE(nav.active()->id() == ViewId::Radio);

    /* Edit the Step row. */
    nav.dispatch(key(KEY_DOWN));
    nav.dispatch(key(KEY_DOWN));
    nav.dispatch(key(KEY_ENTER));
    const uint8_t step = state.step_index;

    nav.openOverlay(ViewId::Macro);
    REQUIRE(nav.activeId() == ViewId::Macro);
    REQUIRE(nav.active()->id() == ViewId::Macro);
    REQUIRE(uiMemory.inlineEdit.owner == ViewId::Radio);

    nav.closeOverlay();
    REQUIRE(nav.active()->id() == ViewId::Radio);
    REQUIRE(uiMemory.inlineEdit.owner == ViewId::Count);
    REQUIRE(selectedRow(nav) == 2);

    /* Still editing: UP steps the value instead of moving the selection. */
    nav.dispatch(key(KEY_UP));
    REQUIRE(state.step_index != step);
    REQUIRE(selectedRow(nav) == 2);

    /* ESC ends the edit, the next one goes back. */
    nav.dispatch(key(KEY_ESC));
    REQUIRE(nav.active()->id() == ViewId::Radio);
    nav.dispatch(key(KEY_ESC));
    REQUIRE(nav.active()->id() == ViewId::Vfo);
}

TEST_CASE("Dismissing an overlay leaves the screen below as it was",
          "[ui][slot]")
{
    ViewSlot slot;
    Navigator nav(slot, uiMemory);
    ProbeOverlay probe;

    nav.setRoot(ViewId::Vfo);
    nav.openOverlay(ViewId::TimeDate);
    nav.dispatch(key(KEY_ENTER)); /* opens the time editor */

    nav.presentOverlay(&probe, 0, 500);
    REQUIRE(nav.active() == &probe);
    View *below = slot.view();
    below->screen().clearDirty();

    nav.tickOverlay(500);
    REQUIRE(nav.overlay() == nullptr);
    REQUIRE(nav.active() == below);
    REQUIRE(below->screen().dirty());

    /* Still editing: ESC closes the editor instead of leaving the screen. */
    nav.dispatch(key(KEY_ESC));
    REQUIRE(nav.activeId() == ViewId::TimeDate);
    nav.dispatch(key(KEY_ESC));
    REQUIRE(nav.activeId() == ViewId::Vfo);
}

TEST_CASE("A VFO frequency entry survives the macro menu", "[ui][slot]")
{
    ViewSlot slot;
    Navigator nav(slot, uiMemory);
    state.tuner_mode = VFO;
    state.channel.rx_frequency = 430000000;
    state.channel.tx_frequency = 430000000;

    nav.setRoot(ViewId::Vfo);
    nav.dispatch(key(KEY_4)); /* opens the entry, retyping RX */
    nav.dispatch(key(KEY_3));

    nav.openOverlay(ViewId::Macro);
    REQUIRE(uiMemory.inlineEdit.owner == ViewId::Vfo);

    nav.closeOverlay();
    REQUIRE(uiMemory.inlineEdit.owner == ViewId::Count);

    nav.dispatch(key(KEY_5));
    nav.dispatch(key(KEY_ENTER)); /* RX done, TX derived from the offset */
    nav.dispatch(key(KEY_ENTER)); /* commit both */
    REQUIRE(state.channel.rx_frequency == 435000000);
    REQUIRE(state.channel.tx_frequency == 435000000);

    /* The entry closed: a digit opens a new one. */
    nav.dispatch(key(KEY_1));
    nav.dispatch(key(KEY_ESC));
    REQUIRE(state.channel.rx_frequency == 435000000);
}

TEST_CASE("A Time & Date entry survives the macro menu", "[ui][slot]")
{
    ViewSlot slot;
    Navigator nav(slot, uiMemory);
    state.settings.utc_timezone = 0;
    state.time = {};

    nav.setRoot(ViewId::Vfo);
    nav.openOverlay(ViewId::TimeDate);
    nav.dispatch(key(KEY_ENTER)); /* opens the entry */
    for (const uint32_t k : { KEY_2, KEY_5, KEY_1, KEY_2 })
        nav.dispatch(key(k));

    nav.openOverlay(ViewId::Macro);
    REQUIRE(uiMemory.inlineEdit.owner == ViewId::TimeDate);

    nav.closeOverlay();
    REQUIRE(uiMemory.inlineEdit.owner == ViewId::Count);

    for (const uint32_t k : { KEY_8, KEY_0, KEY_1, KEY_3, KEY_4, KEY_5 })
        nav.dispatch(key(k));
    nav.dispatch(key(KEY_ENTER)); /* commit */
    REQUIRE(state.time.date == 25);
    REQUIRE(state.time.month == 12);
    REQUIRE(state.time.year == 80);
    REQUIRE(state.time.hour == 13);
    REQUIRE(state.time.minute == 45);

    nav.dispatch(key(KEY_ESC));
    REQUIRE(nav.activeId() == ViewId::Vfo);
}

TEST_CASE("The editor survives the picker", "[ui][slot]")
{
    ViewSlot slot;
    Navigator nav(slot, uiMemory);
    forgetLists();
    memset(state.settings.M17_meta_text, 0,
           sizeof(state.settings.M17_meta_text));

    nav.setRoot(ViewId::Vfo);
    nav.openOverlay(ViewId::M17);
    nav.dispatch(key(KEY_DOWN)); /* Meta Txt */
    nav.dispatch(key(KEY_ENTER));
    REQUIRE(nav.active()->id() == ViewId::TextInput);
    REQUIRE(uiMemory.edit.work != nullptr);

    nav.dispatch(longKey(KEY_1));
    REQUIRE(nav.active()->id() == ViewId::Picker);

    nav.dispatch(key(KEY_RIGHT));
    nav.dispatch(key(KEY_ENTER)); /* U+00A2 */
    REQUIRE(nav.active()->id() == ViewId::TextInput);
    REQUIRE(uiMemory.edit.picked == 0);

    nav.dispatch(key(KEY_ENTER));
    REQUIRE(nav.active()->id() == ViewId::M17);
    REQUIRE(uiMemory.edit.work == nullptr);
    REQUIRE(strcmp(state.settings.M17_meta_text, "\xC2\xA2") == 0);
}

TEST_CASE("The compose draft lives while composing", "[ui][slot]")
{
    REQUIRE(MessageComposeView::open("AB1CD", false));
    REQUIRE(uiMemory.draft != nullptr);
    REQUIRE(strcmp(uiMemory.draft->recipient, "AB1CD") == 0);

    uiMemory.closeDraft();
    REQUIRE(uiMemory.draft == nullptr);
}
