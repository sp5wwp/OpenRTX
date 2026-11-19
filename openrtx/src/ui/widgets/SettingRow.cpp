/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/SettingRow.hpp"
#include "render/DrawCtx.hpp"

namespace ortxui
{

SettingRow::SettingRow()
{
    /* A vertical stack; the engine centres the content and inserts the gutter,
     * and stretches children across the width so the label / value alignments
     * land at the row edges. */
    setAxis(Axis::Column);
    setAlign(Align::Stretch);
    setJustify(Justify::Center);
    setPadding(PAD_X, static_cast<uint8_t>(padY_));
    setGap(static_cast<uint8_t>(gap_));

    label_.setFont(LABEL_FONT);
    label_.setAlign(TEXT_ALIGN_LEFT);
    label_.setOverflow(Label::Overflow::Ellipsize);
    value_.setFont(VALUE_FONT);
    value_.setAlign(TEXT_ALIGN_RIGHT);
    value_.setOverflow(Label::Overflow::Ellipsize);

    addChild(&label_);
    addChild(&value_);
}

void SettingRow::setPad(int16_t padY, int16_t gap)
{
    padY_ = padY;
    gap_ = gap;
    setPadding(PAD_X, static_cast<uint8_t>(padY_));
    setGap(static_cast<uint8_t>(gap_));
}

void SettingRow::bind(Kind kind, const UiText &label, const UiText &value,
                      bool checked, bool selected)
{
    kind_ = kind;
    checked_ = checked;
    selected_ = selected;

    const Sem labelColor = selected ? Sem::OnPrimary : Sem::OnSurface;
    const Sem valueColor = selected ? Sem::OnPrimary : Sem::OnSurfaceMuted;

    label_.setText(label);
    label_.setColor(labelColor);

    /* Only a value row carries the second line; hide it otherwise so the label
     * centres on its own. */
    value_.setFlag(FLAG_HIDDEN, kind_ != Kind::Value);
    if (kind_ == Kind::Value) {
        value_.setText(value);
        value_.setColor(valueColor);
    }
}

bool SettingRow::fitsOneLine(const UiText &label, const UiText &value,
                             int16_t rowW) const
{
    const int16_t avail = static_cast<int16_t>(rowW - 2 * PAD_X);
    if (avail <= 0)
        return false;
    const int need = gfx_getTextWidth(LABEL_FONT, label.str()) + FIT_GAP
                   + gfx_getTextWidth(VALUE_FONT, value.str());
    return need <= avail;
}

int16_t SettingRow::heightFor(Kind kind, const UiText &label,
                              const UiText &value, int16_t rowW) const
{
    const int16_t oneLine =
        static_cast<int16_t>(2 * padY_ + gfx_getFontLineHeight(LABEL_FONT));
    /* A value row keeps its second line only when the two texts do not fit side
     * by side; a fitting row is as short as a plain row (the label font is the
     * taller of the two). */
    if ((kind == Kind::Value) && !fitsOneLine(label, value, rowW))
        return static_cast<int16_t>(oneLine + gap_
                                    + gfx_getFontLineHeight(VALUE_FONT));
    return oneLine;
}

Size SettingRow::natural() const
{
    return { area_.w, static_cast<uint16_t>(heightFor(
                          kind_, label_.text(), value_.text(), area_.w)) };
}

void SettingRow::onLayout()
{
    /* Choose the row shape from the bound text and the assigned width: a value
     * row that fits collapses to a single line (label left, value right, each
     * vertically centred); otherwise the value drops to a second line. Plain /
     * checkbox rows are always the single-line column. */
    const bool oneLine = (kind_ != Kind::Value)
                      || fitsOneLine(label_.text(), value_.text(), area_.w);
    if (oneLine) {
        setAxis(Axis::Row);
        setJustify(Justify::SpaceBetween);
        setAlign(Align::Center);
    } else {
        setAxis(Axis::Column);
        setJustify(Justify::Center);
        setAlign(Align::Stretch);
    }
    Flex::onLayout();
}

void SettingRow::draw(DrawCtx &d)
{
    if (area_.empty())
        return;

    /* A selected row fills over its separator; the others draw a thin one. */
    if (selected_) {
        d.fillRect(area_, Sem::Primary);
    } else {
        const Rect sep = { area_.x, static_cast<int16_t>(area_.bottom()),
                           area_.w, 1 };
        d.fillRect(sep, Sem::Separator);
    }

    if (kind_ != Kind::Checkbox)
        return;

    /* Anti-aliased tick box, vertically centred at the right edge. */
    const int16_t bs = BOX;
    const int16_t bx = static_cast<int16_t>(area_.right() - bs - BOX_INSET);
    const int16_t by = static_cast<int16_t>(area_.y + (area_.h - bs) / 2);
    const Rect box = { bx, by, static_cast<uint16_t>(bs),
                       static_cast<uint16_t>(bs) };

    const Sem border = selected_ ? Sem::OnPrimary : Sem::OnSurfaceMuted;
    const Sem rowBg = selected_ ? Sem::Primary : Sem::Background;
    d.fillRoundRect(box, 2, border);
    d.fillRoundRect(
        { static_cast<int16_t>(bx + 1), static_cast<int16_t>(by + 1),
          static_cast<uint16_t>(bs - 2), static_cast<uint16_t>(bs - 2) },
        1, rowBg);

    if (checked_) {
        const Point p0 = { static_cast<int16_t>(bx + 2),
                           static_cast<int16_t>(by + bs / 2) };
        const Point p1 = { static_cast<int16_t>(bx + bs / 2 - 1),
                           static_cast<int16_t>(by + bs - 3) };
        const Point p2 = { static_cast<int16_t>(bx + bs - 2),
                           static_cast<int16_t>(by + 2) };
        d.lineAA(p0, p1, Sem::Mark);
        d.lineAA(p1, p2, Sem::Mark);
    }
}

} // namespace ortxui
