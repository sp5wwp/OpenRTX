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
    /* Stretched children put the label and value alignments at the row
     * edges. */
    setAxis(Axis::Column);
    setAlign(Align::Stretch);
    setJustify(Justify::Center);
    setPadding(PAD_X, insetY);
    setGap(gutter);

    nameLabel.setFont(LABEL_FONT);
    nameLabel.setAlign(TEXT_ALIGN_LEFT);
    nameLabel.setOverflow(Label::Overflow::Ellipsize);
    valueLabel.setFont(VALUE_FONT);
    valueLabel.setAlign(TEXT_ALIGN_RIGHT);
    valueLabel.setOverflow(Label::Overflow::Ellipsize);

    addChild(&nameLabel);
    addChild(&valueLabel);
}

void SettingRow::setPad(uint8_t inset, uint8_t spacing)
{
    insetY = inset;
    gutter = spacing;
    setPadding(PAD_X, insetY);
    setGap(gutter);
}

void SettingRow::bind(Kind kind, const UiText &label, const UiText &value,
                      bool checked, bool selected)
{
    rowKind = kind;
    isChecked = checked;
    isSelected = selected;

    const Sem labelColor = selected ? Sem::OnPrimary : Sem::OnSurface;
    const Sem valueColor = selected ? Sem::OnPrimary : Sem::OnSurfaceMuted;

    nameLabel.setText(label);
    nameLabel.setColor(labelColor);

    /* Hide the value outside a value row, so the label centres alone. */
    valueLabel.setFlag(FlagHidden, rowKind != Kind::Value);
    if (rowKind == Kind::Value) {
        valueLabel.setText(value);
        valueLabel.setColor(valueColor);
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
        static_cast<int16_t>(2 * insetY + gfx_getFontLineHeight(LABEL_FONT));
    /* The label font is the taller, so a value row on one line is as high as
     * a plain row. */
    if ((kind == Kind::Value) && !fitsOneLine(label, value, rowW))
        return static_cast<int16_t>(oneLine + gutter
                                    + gfx_getFontLineHeight(VALUE_FONT));
    return oneLine;
}

Size SettingRow::natural() const
{
    return { bounds.w,
             static_cast<uint16_t>(heightFor(rowKind, nameLabel.text(),
                                             valueLabel.text(), bounds.w)) };
}

void SettingRow::onLayout()
{
    /* One line, label left and value right, unless a value does not fit. */
    const bool oneLine = (rowKind != Kind::Value)
                      || fitsOneLine(nameLabel.text(), valueLabel.text(),
                                     bounds.w);
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
    if (bounds.empty())
        return;

    /* A selected row fills over its separator; the others draw a thin one. */
    if (isSelected) {
        d.fillRect(bounds, Sem::Primary);
    } else {
        const Rect sep = { bounds.x, static_cast<int16_t>(bounds.bottom()),
                           bounds.w, 1 };
        d.fillRect(sep, Sem::Separator);
    }

    if (rowKind != Kind::Checkbox)
        return;

    /* Anti-aliased tick box, vertically centred at the right edge. */
    const int16_t bs = BOX;
    const int16_t bx = static_cast<int16_t>(bounds.right() - bs - BOX_INSET);
    const int16_t by = static_cast<int16_t>(bounds.y + (bounds.h - bs) / 2);
    const Rect box = { bx, by, static_cast<uint16_t>(bs),
                       static_cast<uint16_t>(bs) };

    const Sem border = isSelected ? Sem::OnPrimary : Sem::OnSurfaceMuted;
    const Sem rowBg = isSelected ? Sem::Primary : Sem::Background;
    d.fillRoundRect(box, 2, border);
    d.fillRoundRect(
        { static_cast<int16_t>(bx + 1), static_cast<int16_t>(by + 1),
          static_cast<uint16_t>(bs - 2), static_cast<uint16_t>(bs - 2) },
        1, rowBg);

    if (isChecked) {
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
