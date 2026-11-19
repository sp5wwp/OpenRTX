/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_UITEXT_HPP
#define ORTX_UI_UITEXT_HPP

#include <cstddef>
#include <cstdint>

extern "C" {
#include "ui/ui_strings.h"
}
#include "core/voicePromptUtils.h"

namespace ortxui
{

/**
 * Text shown by a widget: either a string table entry, resolved in the
 * current language each time it is read, or plain text such as a channel
 * name. Static labels are table entries, so they follow the language and
 * are announced by their voice prompt.
 */
class UiText
{
public:
    /** Destructor. */
    ~UiText() = default;

    /**
     * Empty text.
     */
    constexpr UiText() = default;

    /**
     * Plain text, shown as is.
     *
     * @param raw: text, may be NULL.
     */
    constexpr UiText(const char *raw) : raw_(raw)
    {
    }

    /**
     * A string table entry.
     *
     * @param id: entry index in stringsTable_t.
     * @return the text.
     */
    static constexpr UiText entry(uint8_t id)
    {
        UiText t;
        t.id_ = id;
        return t;
    }

    /**
     * Resolve the text.
     *
     * @return the current language's entry, the English one if that is
     * missing, or the plain text; never NULL.
     */
    const char *str() const;

    /**
     * Check whether the text is a string table entry.
     *
     * @return true for a table entry.
     */
    bool isEntry() const
    {
        return id_ != NO_ID;
    }

    /**
     * Check whether the text was set: an entry, or plain text that is not
     * NULL. Lets a list tell an empty value from no value at all.
     *
     * @return true if set.
     */
    bool isSet() const
    {
        return isEntry() || (raw_ != nullptr);
    }

    /**
     * Check whether the text resolves to an empty string.
     *
     * @return true if empty.
     */
    bool empty() const
    {
        return str()[0] == '\0';
    }

    /**
     * Queue the text's voice prompt: the entry's prompt, or the plain text
     * matched or spelled by vp_announceText().
     *
     * @param flags: voice prompt queue flags.
     */
    void announce(vpQueueFlags flags) const;

    /**
     * Compare two texts by entry, or by content for plain text.
     *
     * @param other: text to compare with.
     * @return true if they are the same text.
     */
    bool operator==(const UiText &other) const;

private:
    static constexpr uint8_t NO_ID = 0xFF; ///< Marks plain text

    const char *raw_ = nullptr;            ///< Plain text, when not an entry
    uint8_t id_ = NO_ID;                   ///< Entry index in stringsTable_t
};

} // namespace ortxui

/**
 * The UiText for a string table field, e.g. STR(settings).
 */
#define STR(field)                                          \
    ::ortxui::UiText::entry(offsetof(stringsTable_t, field) \
                            / sizeof(const char *))

#endif // ORTX_UI_UITEXT_HPP
