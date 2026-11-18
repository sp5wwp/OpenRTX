/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_SETTINGSLISTVIEW_HPP
#define ORTX_UI_SETTINGSLISTVIEW_HPP

#include <cstdint>
#include <cstddef>
#include "core/View.hpp"
#include "layout/Flex.hpp"
#include "widgets/TopBar.hpp"
#include "widgets/List.hpp"

namespace ortxui
{

/**
 * Base for the Settings value-list screens (Display, Radio, FM, M17, GPS).
 *
 * These all share one interaction: a List of rows where ENTER either opens a
 * value for editing (UP/DOWN or the knob step it, ENTER/ESC close), toggles a
 * checkbox, or activates an action; and the value of the row being edited is
 * shown wrapped in <angle brackets>. This class owns that state machine and the
 * screen/root/top-bar/list scaffolding; each derived view supplies only its
 * rows and the per-row format/adjust logic through the hooks below.
 *
 * Row kinds:
 *  - Stepper  : ENTER edits; onAdjust() changes the value; formatValue() renders
 *               the bare text and the base adds the <..> marker while editing.
 *  - Checkbox : ENTER toggles items_[row].checked and calls onToggle().
 *  - Action   : ENTER returns onActivate()'s NavIntent (e.g. push a sub-editor).
 *  - Custom   : ENTER opens an in-place editor the view fully owns -- the base
 *               calls onBeginEdit() then routes every editing event to
 *               onEditEvent() and re-renders via refreshCustom().
 */
class SettingsListView : public View
{
public:
    Screen &screen() override
    {
        return screen_;
    }
    NavIntent onEvent(const Event &e) override;

protected:
    enum class RowKind : uint8_t { Stepper, Checkbox, Action, Custom };

    /**
     * Wire the shared scaffolding: derived build() seeds items_ (labels and
     * values) first, then calls this with the title and its item array.
     *
     * @param title: screen title.
     * @param items: row table, borrowed.
     * @param count: number of rows.
     */
    void buildList(const UiText &title, ListItem *items, uint8_t count);

    /* ---- required hooks ---- */
    /** Render row's BARE value (no <> marker) into out. */
    virtual void formatValue(uint8_t row, char *out, size_t cap) = 0;
    /** Store a composed value string into the view's bufs_[row]. */
    virtual void setValueText(uint8_t row, const char *s) = 0;
    /** Step a Stepper row's value by dir (+1/-1) and apply it to state. */
    virtual void onAdjust(uint8_t row, int dir) = 0;

    /* ---- optional hooks (defaults suit a pure-stepper view) ---- */
    virtual RowKind rowKind(uint8_t) const
    {
        return RowKind::Stepper;
    }
    virtual void onToggle(uint8_t /*row*/, bool /*on*/)
    {
    }
    virtual NavIntent onActivate(uint8_t /*row*/)
    {
        return NavIntent::none();
    }
    virtual void onBeginEdit(uint8_t /*row*/)
    {
    }
    /** A Custom row consumes its editing events here; return true if handled. */
    virtual bool onEditEvent(const Event & /*e*/)
    {
        return false;
    }
    /** Re-render a Custom row's (bracketed) value string. */
    virtual void refreshCustom(uint8_t /*row*/)
    {
    }

    /* ---- helpers the derived views call ---- */
    void
    refreshValue(uint8_t row); //< formatValue + <> wrap + vpSay + invalidate
    void beginEdit();
    void endEdit();

    Screen screen_;
    Flex root_;
    TopBar bar_;
    List list_;
    bool editing_ = false;
    uint8_t editRow_ = 0;
};

} // namespace ortxui

#endif /* ORTX_UI_SETTINGSLISTVIEW_HPP */
