/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "widgets/List.hpp"
#include "widgets/ListModel.hpp"

using namespace ortxui;

/**
 * A model of identical rows.
 */
class RowsModel : public ListModel
{
public:
    /**
     * Constructor.
     *
     * @param rows: number of rows.
     */
    explicit RowsModel(uint16_t rows) : rows_(rows)
    {
    }

    /** Get the number of rows. */
    uint16_t rowCount() const override
    {
        return rows_;
    }

    /** Fill a row with a fixed label. */
    void rowAt(uint16_t index, ListItem &out) const override
    {
        (void)index;
        out.label = "Row";
    }

private:
    uint16_t rows_; ///< Number of rows
};

TEST_CASE("A list keeps positions past row 255", "[ui][list]")
{
    RowsModel model(300);
    List list;
    list.setArea({ 0, 0, 160, 100 });
    list.setModel(&model);

    list.setPosition({ 290, 285 });
    const ListPos pos = list.position();
    REQUIRE(pos.selected == 290);
    REQUIRE(pos.top == 285);
}
