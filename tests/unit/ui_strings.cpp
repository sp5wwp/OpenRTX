/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include "base/UiText.hpp"

using ortxui::UiText;

/** Number of entries in stringsTable_t. */
static constexpr size_t kEntries = sizeof(stringsTable_t)
                                 / sizeof(const char *);

/**
 * Get a string table's entries as an array.
 *
 * @param table: string table.
 * @return its entries.
 */
static const char *const *entries(const stringsTable_t *table)
{
    return reinterpret_cast<const char *const *>(table);
}

TEST_CASE("Every English string table entry is set", "[ui]")
{
    for (size_t i = 0; i < kEntries; i++) {
        const char *s = entries(&englishStrings)[i];
        INFO("entry " << i);
        REQUIRE(s != nullptr);
        REQUIRE(s[0] != '\0');
    }
}

TEST_CASE("UiText resolves entries and plain text", "[ui]")
{
    const stringsTable_t *saved = currentLanguage;
    stringsTable_t other = englishStrings;

    SECTION("an entry resolves in the current language")
    {
        other.settings = "Ajustes";
        currentLanguage = &other;
        REQUIRE(strcmp(STR(settings).str(), "Ajustes") == 0);
        REQUIRE(STR(settings).isEntry());
    }

    SECTION("an empty entry falls back to English")
    {
        other.settings = "";
        currentLanguage = &other;
        REQUIRE(strcmp(STR(settings).str(), englishStrings.settings) == 0);
    }

    SECTION("plain text passes through")
    {
        const UiText t("CH 12");
        REQUIRE(!t.isEntry());
        REQUIRE(strcmp(t.str(), "CH 12") == 0);
        REQUIRE(t == UiText("CH 12"));
        REQUIRE(UiText().empty());
    }

    currentLanguage = saved;
}
