/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_SETTINGSLISTVIEW_HPP
#define ORTX_UI_SETTINGSLISTVIEW_HPP

#include <cstdint>
#include <cstddef>
#include "views/UiMemory.hpp"
#include "views/View.hpp"
#include "core/voicePrompts.h"
#include "layout/Flex.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/List.hpp"

namespace ortxui
{

/**
 * Base for the settings screens: a titled list of rows where ENTER edits a
 * value (the arrows or the knob step it, ENTER or ESC close), toggles a
 * checkbox or opens a screen. The value being edited shows in <brackets>, and
 * a change speaks the row's label, then its value. Derived views supply the
 * rows and the per-row format and adjust hooks.
 */
class SettingsListView : public View
{
public:
    /** How a row is edited. */
    enum class RowKind : uint8_t {
        Stepper,  ///< ENTER edits, the arrows step the value
        Checkbox, ///< ENTER toggles
        Action,   ///< ENTER acts, usually opening a screen
        Custom,   ///< The view handles the edit itself
    };

    /** Constructor. */
    SettingsListView() = default;

    /** Destructor. */
    ~SettingsListView() = default;

    /** The view's widget-tree root. */
    Screen &screen() override
    {
        return tree;
    }

    /** Edit, toggle or open the selected row, or go back on ESC. */
    NavIntent onEvent(const Event &e) override;

    /** The settings list, whose position UiMemory keeps. */
    List *list() override
    {
        return &listBox;
    }

    /** Keep the row being edited while destroyed. */
    void save() const override;

    /** Reopen the row edit kept by save(). */
    void restore() override;

protected:
    static constexpr uint8_t ROWS_MAX = 8;    ///< Most rows a screen has
    static constexpr size_t VALUE_SIZE = 24;  ///< Value text with its brackets
    static constexpr size_t CUSTOM_EDIT = 16; ///< Largest Custom row edit state

    /**
     * Build the screen around the rows. Derived build() sets the labels, and
     * the checkbox or value of rows that do not show the base's value text,
     * then calls this; every other row shows values()[row].
     *
     * @param title: screen title.
     * @param items: row table, borrowed.
     * @param count: number of rows, at most ROWS_MAX.
     */
    void buildList(const UiText &title, ListItem *items, uint8_t count);

    /**
     * Render a row's value without the edit brackets.
     *
     * @param row: row.
     * @param out: output buffer.
     * @param cap: output buffer size.
     */
    virtual void formatValue(uint8_t row, char *out, size_t cap)
    {
        (void)row;
        (void)cap;
        out[0] = '\0';
    }

    /**
     * Step a Stepper row's value and apply it to state.
     *
     * @param row: row.
     * @param dir: +1 up, -1 down.
     */
    virtual void onAdjust(uint8_t row, int8_t dir)
    {
        (void)row;
        (void)dir;
    }

    /**
     * Get how a row is edited.
     *
     * @param row: row.
     * @return the row kind.
     */
    virtual RowKind rowKind(uint8_t row) const
    {
        (void)row;
        return RowKind::Stepper;
    }

    /**
     * Apply a Checkbox row's new state.
     *
     * @param row: row.
     * @param on: new state.
     */
    virtual void onToggle(uint8_t row, bool on)
    {
        (void)row;
        (void)on;
    }

    /**
     * Act on an Action row.
     *
     * @param row: row.
     * @return where to navigate.
     */
    virtual NavIntent onActivate(uint8_t row)
    {
        (void)row;
        return NavIntent::none();
    }

    /**
     * Prepare the editor of a row about to be edited.
     *
     * @param row: row.
     */
    virtual void onBeginEdit(uint8_t row)
    {
        (void)row;
    }

    /**
     * Handle an event while a Custom row is edited.
     *
     * @param e: event.
     * @return true if handled.
     */
    virtual bool onEditEvent(const Event &e)
    {
        (void)e;
        return false;
    }

    /**
     * Render a Custom row's value, bracketed while it is edited.
     *
     * @param row: row.
     * @param speak: speak the value of the row being edited.
     */
    virtual void refreshCustom(uint8_t row, bool speak)
    {
        (void)row;
        (void)speak;
    }

    /**
     * Store the editor state of the Custom row being edited.
     *
     * @param e: where to put() it.
     */
    virtual void saveCustom(EditBlob<CUSTOM_EDIT> &e) const
    {
        (void)e;
    }

    /**
     * Reopen the Custom row editor from saveCustom().
     *
     * @param e: what saveCustom() stored.
     */
    virtual void restoreCustom(const EditBlob<CUSTOM_EDIT> &e)
    {
        (void)e;
    }

    /**
     * Speak a row's value after it changed: the label at the descriptive
     * level, then the value. Views override it for values with their own
     * voice prompt helper.
     *
     * @param row: row.
     * @param value: value as shown.
     * @param flags: voice prompt queue flags of the current level.
     */
    virtual void announceValue(uint8_t row, const UiText &value,
                               vpQueueFlags flags);

    /**
     * Render a row's value, bracketed while it is edited, and repaint.
     *
     * @param row: row.
     * @param speak: speak the value of the row being edited.
     */
    void refreshValue(uint8_t row, bool speak = true);

    /**
     * Speak a row's value through announceValue() at the current level.
     *
     * @param row: row.
     * @param value: value as shown.
     */
    void speakValue(uint8_t row, const UiText &value);

    /**
     * Get the value text buffer of a row.
     *
     * @param row: row.
     * @return the buffer, VALUE_SIZE bytes.
     */
    char *values(uint8_t row)
    {
        return valueText[row];
    }

    /** Start editing the selected row. */
    void beginEdit();

    /** Stop editing and show the row's value unbracketed. */
    void endEdit();

    Screen tree;          ///< Widget tree root
    Flex root;            ///< Top bar over the list
    List listBox;         ///< The rows
    bool editing = false; ///< A row is being edited
    uint8_t editRow = 0;  ///< Row being edited

private:
    /** A row edit kept while the view is destroyed. */
    struct RowEdit {
        EditBlob<CUSTOM_EDIT> custom; ///< Editor state of a Custom row
        uint8_t row;                  ///< Row being edited
    };

    static constexpr int16_t ROW_PAD = 4;      ///< Vertical inset of each row

    ListItem *rows = nullptr;                  ///< Row table, borrowed
    char valueText[ROWS_MAX][VALUE_SIZE] = {}; ///< Value text of each row
};

} // namespace ortxui

#endif /* ORTX_UI_SETTINGSLISTVIEW_HPP */
