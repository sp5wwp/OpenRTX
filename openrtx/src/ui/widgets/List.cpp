/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/List.hpp"
#include "core/Event.hpp"
#include "render/DrawCtx.hpp"
#include "interfaces/keyboard.h"
#include "core/state.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"
#include "widgets/Scratch.hpp"

namespace ortxui
{

void List::setItems(const ListItem *items, uint16_t count)
{
    items_ = items;
    model_ = nullptr;
    count_ = count;
    selected_ = 0;
    top_ = 0;
    invalidate();
}

void List::setModel(const ListModel *model)
{
    model_ = model;
    items_ = nullptr;
    count_ = 0;
    selected_ = 0;
    top_ = 0;
    invalidate();
}

ListPos List::position() const
{
    return { selected_, top_ };
}

void List::setPosition(const ListPos &p)
{
    const uint16_t n = rows();
    selected_ = (p.selected < n) ? p.selected : 0;
    const uint16_t last = selectable_ ? selected_ : maxTop();
    top_ = (p.top <= last) ? p.top : last;
    invalidate();
}

ListItem *List::selectedItem()
{
    /* Model-backed rows are read-only (fetched into a scratch on draw), so
     * there is no persistent item to mutate; callers read selected() instead. */
    if ((items_ == nullptr) || (selected_ >= count_))
        return nullptr;
    /* The item table is borrowed and owned mutably by the view. */
    return const_cast<ListItem *>(&items_[selected_]);
}

void List::getItem(uint16_t i, ListItem &out) const
{
    if (model_ != nullptr)
        model_->rowAt(i, out);
    else if (items_ != nullptr)
        out = items_[i];
}

SettingRow::Kind List::kindOf(const ListItem &it)
{
    if (it.checkbox)
        return SettingRow::Kind::Checkbox;
    if (it.value.isSet())
        return SettingRow::Kind::Value;
    return SettingRow::Kind::Plain;
}

int16_t List::rowHeight(uint16_t i) const
{
    ListItem it;
    getItem(i, it);
    SettingRow &row = Scratch::get<SettingRow>();
    row.setPad(padY_, gap_);
    return row.heightFor(kindOf(it), it.label, it.value, area_.w);
}

void List::scrollToSelected()
{
    /* Rows are variable height, so scroll by pixels: pull the top down to the
     * selection if it is above the window, else advance the top until the
     * selection's row fits within the viewport height. */
    if (selected_ < top_) {
        top_ = selected_;
        return;
    }
    while (top_ < selected_) {
        int sum = 0;
        for (uint16_t i = top_; i <= selected_; i++)
            sum += rowHeight(i);
        if (sum <= static_cast<int>(area_.h))
            break;
        top_++;
    }
}

void List::setSelected(uint16_t i, bool announce)
{
    const uint16_t n = rows();
    if (n == 0)
        return;
    if (i >= n)
        i = static_cast<uint16_t>(n - 1);
    if (i == selected_)
        return;

    selected_ = i;
    scrollToSelected();
    invalidate();
    if (announce)
        announceSelection();
}

void List::moveSelection(int dir)
{
    const int n = static_cast<int>(rows());
    if (n == 0)
        return;
    int idx = static_cast<int>(selected_) + ((dir >= 0) ? 1 : -1);
    if (idx < 0)
        idx = n - 1; /* wrap to the bottom */
    else if (idx >= n)
        idx = 0;     /* wrap to the top */

    selected_ = static_cast<uint16_t>(idx);
    scrollToSelected();
    invalidate();
    announceSelection();
}

void List::scroll(int dir)
{
    const uint16_t last = maxTop();
    uint16_t top = top_;
    if ((dir > 0) && (top < last))
        top++;
    else if ((dir < 0) && (top > 0))
        top--;

    if (top == top_)
        return;
    top_ = top;
    invalidate();
}

uint16_t List::maxTop() const
{
    /* Walk up from the last row while the rows below still fit. */
    const uint16_t n = rows();
    uint16_t top = n;
    int sum = 0;
    while (top > 0) {
        sum += rowHeight(static_cast<uint16_t>(top - 1));
        if (sum > static_cast<int>(area_.h))
            break;
        top--;
    }

    /* A last row taller than the viewport still shows from its top. */
    if ((top == n) && (n > 0))
        top = static_cast<uint16_t>(n - 1);
    return top;
}

void List::announceSelection() const
{
    /* Speak the freshly-selected row (label, then value or checkbox state) when
     * voice prompts are enabled — the one place that covers navigation in every
     * list-backed view. Dormant at the default vpNone level. */
    if (!selectable_ || (state.settings.vpLevel < vpLow))
        return;

    const uint16_t n = rows();
    if (selected_ >= n)
        return;

    ListItem it;
    if (model_ != nullptr)
        model_->rowAt(selected_, it);
    else if (items_ != nullptr)
        it = items_[selected_];
    else
        return;

    if (it.label.empty())
        return;

    const enum vpQueueFlags flags = vp_getVoiceLevelQueueFlags();
    vp_flush();
    it.label.announce(flags);
    if (it.checkbox)
        (it.checked ? STR(on) : STR(off)).announce(vpqDefault);
    else if (!it.value.empty())
        it.value.announce(vpqDefault);
    vp_play();
}

bool List::onEvent(const Event &e)
{
    int dir = 0;
    if (e.kind == EvKind::Encoder) {
        dir = e.encoder;
    } else if ((e.kind == EvKind::Key) || (e.kind == EvKind::KeyRepeat)) {
        /* A held arrow keeps scrolling. */
        if ((e.keys & KEY_DOWN) != 0u)
            dir = +1;
        else if ((e.keys & KEY_UP) != 0u)
            dir = -1;
    }
    if (dir == 0)
        return false;

    /* Without a selection there is nothing to move but the window. */
    if (selectable_)
        moveSelection(dir);
    else
        scroll(dir);
    return true;
}

void List::draw(DrawCtx &d)
{
    const uint16_t n = rows();
    if ((n == 0) || ((items_ == nullptr) && (model_ == nullptr)))
        return;

    /* Paint each visible row through the scratch SettingRow: bind it to the
     * item, size it to the item's content, lay it out and paint its subtree.
     * Rows stack by their own heights, so a two-line value row is taller than
     * a plain / checkbox row while keeping the same padding. */
    SettingRow &scratchRow = Scratch::get<SettingRow>();
    scratchRow.setPad(padY_, gap_);

    int16_t y = area_.y;
    uint16_t i = top_;
    for (; i < n; i++) {
        ListItem it;
        getItem(i, it);
        const SettingRow::Kind kind = kindOf(it);
        const int16_t h = scratchRow.heightFor(kind, it.label, it.value,
                                               area_.w);
        if (y > area_.bottom())
            break;

        /* A row outside the clip, as in another band, draws nothing, but
         * still moves the next rows down and counts as shown. */
        const Rect row = { area_.x, y, area_.w, static_cast<uint16_t>(h) };
        y = static_cast<int16_t>(y + h);
        if (!row.empty() && intersect(d.clip(), row).empty())
            continue;

        const bool selected = selectable_ && (i == selected_);
        scratchRow.bind(kind, it.label, it.value, it.checked, selected);
        scratchRow.setArea(row);
        scratchRow.onLayout();
        scratchRow.paintTree(d);
    }

    /* Scroll indicator when the list overflows the viewport (there is a row
     * above the window or one that did not fit below). The scrolling lists are
     * uniform-height, so a count-based thumb tracks the pixels exactly. */
    const bool overflow = (top_ > 0) || (i < n);
    if (overflow) {
        const uint16_t vis = static_cast<uint16_t>(i - top_);
        const int16_t barX = static_cast<int16_t>(area_.right() - 1);
        const int trackH = static_cast<int>(area_.h);
        int thumbH = (n > 0) ? trackH * vis / n : trackH;
        if (thumbH < 2)
            thumbH = 2;
        const int16_t thumbY =
            static_cast<int16_t>(area_.y + trackH * top_ / n);

        const Rect track = { barX, area_.y, 2, area_.h };
        d.fillRect(track, Sem::Surface);
        const Rect thumb = { barX, thumbY, 2, static_cast<uint16_t>(thumbH) };
        d.fillRect(thumb, Sem::OnSurfaceMuted);
    }
}

} // namespace ortxui
