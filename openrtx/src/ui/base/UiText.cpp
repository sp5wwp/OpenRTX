/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "base/UiText.hpp"

#include <cstring>

namespace ortxui
{

static_assert(sizeof(stringsTable_t) / sizeof(const char *) < 0xFF,
              "string table too large for UiText ids");

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

const char *UiText::str() const
{
    if (!isEntry())
        return (raw_ != nullptr) ? raw_ : "";

    /* A language pack may leave an entry untranslated. */
    const char *s = entries(currentLanguage)[id_];
    if ((s == nullptr) || (s[0] == '\0'))
        s = entries(&englishStrings)[id_];

    return (s != nullptr) ? s : "";
}

void UiText::announce(vpQueueFlags flags) const
{
    if (isEntry())
        vp_announceStringTableEntry(&entries(currentLanguage)[id_], flags);
    else
        vp_announceText(str(), flags);
}

bool UiText::operator==(const UiText &other) const
{
    if (isEntry() || other.isEntry())
        return id_ == other.id_;

    return strcmp(str(), other.str()) == 0;
}

} // namespace ortxui
