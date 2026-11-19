/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "base/Event.hpp"
#include "interfaces/keyboard.h"
#include "render/DrawCtx.hpp"
#include "widgets/List.hpp"
#include "widgets/ListModel.hpp"

using namespace ortxui;

/**
 * A model of identical rows that counts how often they are read.
 */
class RowsModel : public ListModel
{
public:
    /**
     * Constructor.
     *
     * @param rows: number of rows.
     * @param plain: whether the model says its rows are plain labels.
     */
    RowsModel(uint16_t rows, bool plain) : rows_(rows), plain_(plain)
    {
    }

    /** Get the number of rows. */
    uint16_t rowCount() const override
    {
        return rows_;
    }

    /** Fill a row with a fixed label and count the read. */
    void rowAt(uint16_t index, ListItem &out) const override
    {
        (void)index;
        out.label = "Row";
        reads++;
    }

    /** Whether every row is a plain label. */
    bool plainRows() const override
    {
        return plain_;
    }

    mutable uint32_t reads = 0; ///< Rows read so far

private:
    uint16_t rows_; ///< Number of rows
    bool plain_;    ///< Rows are plain labels
};

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

/** Height of the list in the tests, a few rows. */
static constexpr uint16_t LIST_H = 100;

/** Most rows a step may read: one window of rows, with a margin. */
static constexpr uint32_t WINDOW_READS = 20;

TEST_CASE("A list keeps positions past row 255", "[ui][list]")
{
    RowsModel model(300, true);
    List list;
    list.setArea({ 0, 0, 160, LIST_H });
    list.setModel(&model);

    list.setPosition({ 290, 285 });
    const ListPos pos = list.position();
    REQUIRE(pos.selected == 290);
    REQUIRE(pos.top == 285);
}

TEST_CASE("Wrapping a long list reads one window of rows", "[ui][list]")
{
    for (const bool plain : { true, false }) {
        INFO("plain rows: " << plain);
        RowsModel model(1000, plain);
        List list;
        list.setArea({ 0, 0, 160, LIST_H });
        list.setModel(&model);

        list.onEvent(key(KEY_UP));
        REQUIRE(list.selected() == 999);
        REQUIRE(list.position().top > 990);
        REQUIRE(model.reads <= (plain ? 0u : WINDOW_READS));

        model.reads = 0;
        list.onEvent(key(KEY_DOWN));
        REQUIRE(list.selected() == 0);
        REQUIRE(list.position().top == 0);
        REQUIRE(model.reads <= (plain ? 0u : WINDOW_READS));
    }
}

TEST_CASE("Drawing a band reads only the plain rows in it", "[ui][list]")
{
    RowsModel model(1000, true);
    List list;
    list.setArea({ 0, 0, 160, LIST_H });
    list.setModel(&model);

    DrawCtx band({ 0, 48, 160, 16 });
    list.draw(band);
    REQUIRE(model.reads > 0);
    REQUIRE(model.reads <= 3);
}
