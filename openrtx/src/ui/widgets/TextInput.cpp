/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/TextInput.hpp"
#include "core/Event.hpp"
#include "core/Layout.hpp"
#include "render/DrawCtx.hpp"
#include "style/Symbols.hpp"
#include "core/input.h"
#include "core/utils.h"
#include "interfaces/keyboard.h"

#include <cstring>

namespace ortxui
{

/** Longest wrapped line measured or copied at once. */
static constexpr uint16_t LINE_BUF = 128;

static constexpr int16_t LINE_GAP = 2;     ///< Pixels between wrapped lines
static constexpr uint16_t SCROLL_W = 2;    ///< Width of the scroll indicator
static constexpr uint16_t SCROLL_GAP = 1;  ///< Gap between text and indicator
static constexpr int16_t SCROLL_MIN_H = 2; ///< Shortest scroll thumb

/** The delete slot of the cycle: only under the cursor, applied on a move. */
static constexpr char DELETE_CELL = 0x7F;
static constexpr char DELETE_GLYPH[] = SYMBOL_BACKSPACE; ///< Delete slot glyph

/** Keys that keep acting while held in a text field. */
static constexpr uint32_t REPEAT_KEYS = KEY_STAR | KEY_UP | KEY_DOWN | KEY_LEFT
                                      | KEY_RIGHT;

/**
 * Tell whether the keypad has neither arrows nor digits, so the knob changes
 * the character and ENTER steps to the next one.
 *
 * @return true on a knob-only keypad.
 */
static constexpr bool knobOnly()
{
    return !kbdHasArrows() && !kbdHasNumeric();
}

/**
 * Append bytes to a NUL-terminated string, if they fit.
 *
 * @param o: end of the string, moved past the bytes.
 * @param rem: space left at `o`, including the NUL.
 * @param s: bytes.
 * @param n: number of bytes.
 * @return false if they did not fit.
 */
static bool append(char *&o, size_t &rem, const char *s, size_t n)
{
    if (n >= rem)
        return false;
    memcpy(o, s, n);
    o += n;
    rem -= n;
    *o = '\0';
    return true;
}

/**
 * Measure a span of a buffer.
 *
 * @param buf: text.
 * @param start: first byte of the span.
 * @param end: byte after the span.
 * @param font: font.
 * @return the pixel width of buf[start, end).
 */
static uint16_t spanWidth(const char *buf, uint16_t start, uint16_t end,
                          fontSize_t font)
{
    char tmp[LINE_BUF];
    uint16_t n = static_cast<uint16_t>(end - start);
    if (n >= sizeof(tmp))
        n = sizeof(tmp) - 1;
    memcpy(tmp, buf + start, n);
    tmp[n] = '\0';
    return gfx_getTextWidth(font, tmp);
}

void TextInput::configure(char *buf, uint16_t cap, const Charset &cs, Mode mode,
                          fontSize_t font)
{
    buf_ = buf;
    cap_ = cap;
    cs_ = &cs;
    mode_ = mode;
    font_ = font;
    len_ = 0;
    cursor_ = 0;
    topRow_ = 0;
    tapActive_ = false;
    tapKeyIdx_ = -1;
    enterDown_ = false;
    /* Leave any prior slot-mode state behind so a reused widget accepts text. */
    slotMode_ = false;
    slotCount_ = 0;
    slotCursor_ = 0;
}

void TextInput::configureSlots(char *buf, uint16_t cap, const char *mask,
                               char placeholder, fontSize_t font)
{
    if ((buf == nullptr) || (cap == 0) || (mask == nullptr))
        return;

    buf_ = buf;
    cap_ = cap;
    cs_ = nullptr;
    mode_ = Mode::SingleLine;
    font_ = font;
    slotMode_ = true;
    placeholder_ = placeholder;
    tapActive_ = false;
    tapKeyIdx_ = -1;
    enterDown_ = false;

    /* Lay the mask into the buffer and note each slot's offset. A mask longer
     * than the buffer or MAX_SLOTS is cut short, which slotCount() shows. */
    slotCount_ = 0;
    uint16_t i = 0;
    for (; (mask[i] != '\0') && (i + 1 < cap_); i++) {
        if (mask[i] == '9') {
            if (slotCount_ >= MAX_SLOTS)
                break; /* mask has more slots than we can track */
            buf_[i] = placeholder_;
            slotPos_[slotCount_++] = i;
        } else {
            buf_[i] = mask[i];
        }
    }
    buf_[i] = '\0';
    len_ = i;
    slotCursor_ = 0;
    syncSlotCursor();
}

int TextInput::slotToPos(uint16_t ordinal) const
{
    return (ordinal < slotCount_) ? static_cast<int>(slotPos_[ordinal]) : -1;
}

void TextInput::syncSlotCursor()
{
    /* Keep the render cursor on the active slot; when the cursor has run off the
     * end (all slots filled) rest the bracket on the last slot. */
    uint16_t ord = slotCursor_;
    if (ord >= slotCount_)
        ord = (slotCount_ > 0) ? static_cast<uint16_t>(slotCount_ - 1) : 0;
    const int p = slotToPos(ord);
    cursor_ = (p >= 0) ? static_cast<uint16_t>(p) : 0;
}

void TextInput::putDigit(uint8_t d)
{
    if (!slotMode_ || (d > 9) || (slotCursor_ >= slotCount_))
        return;
    const int p = slotToPos(slotCursor_);
    if (p < 0)
        return;
    buf_[p] = static_cast<char>('0' + d);
    slotCursor_++;
    syncSlotCursor();
}

void TextInput::setSlotDigit(uint16_t ordinal, int d)
{
    if (!slotMode_ || (ordinal >= slotCount_))
        return;
    const int p = slotToPos(ordinal);
    if (p < 0)
        return;
    /* Out-of-range digits fall back to the placeholder rather than wrapping to a
     * wrong digit (e.g. 15 must not become '5'). */
    buf_[p] = ((d < 0) || (d > 9)) ? placeholder_ : static_cast<char>('0' + d);
}

int TextInput::slotDigit(uint16_t ordinal) const
{
    if (!slotMode_ || (ordinal >= slotCount_))
        return -1;
    const int p = slotToPos(ordinal);
    if (p < 0)
        return -1;
    const char c = buf_[p];
    return ((c >= '0') && (c <= '9')) ? (c - '0') : -1;
}

uint16_t TextInput::filledSlots() const
{
    uint16_t n = 0;
    while ((n < slotCount_) && (slotDigit(n) >= 0))
        n++;
    return n;
}

void TextInput::setCursorSlot(uint16_t ordinal)
{
    if (!slotMode_)
        return;
    slotCursor_ = (ordinal <= slotCount_) ? ordinal : slotCount_;
    syncSlotCursor();
}

void TextInput::clearSlots()
{
    if (!slotMode_)
        return;
    for (uint16_t o = 0; o < slotCount_; o++)
        buf_[slotPos_[o]] = placeholder_;
    slotCursor_ = 0;
    syncSlotCursor();
}

void TextInput::begin()
{
    if ((buf_ == nullptr) || (cap_ == 0))
        return;
    len_ = static_cast<uint16_t>(strnlen(buf_, cap_ - 1));
    buf_[len_] = '\0';
    cursor_ = 0;
    seedBlank();
    topRow_ = 0;
    tapActive_ = false;
    tapKeyIdx_ = -1;
}

void TextInput::seedBlank()
{
    if ((len_ == 0) && (cap_ >= 2)) {
        buf_[0] = ' ';
        buf_[1] = '\0';
        len_ = 1;
        cursor_ = 0;
    }
}

void TextInput::resume(uint16_t cursor, uint16_t topRow)
{
    begin();
    cursor_ = (cursor <= len_) ? cursor : len_;
    topRow_ = topRow;
}

uint16_t TextInput::cellLen(uint16_t pos) const
{
    if ((buf_ == nullptr) || (pos >= len_))
        return 0;
    const uint8_t b = static_cast<uint8_t>(buf_[pos]);
    if ((b & 0x80u) == 0x00u)
        return 1; /* ASCII / DELETE_CELL */
    if ((b & 0xE0u) == 0xC0u)
        return 2;
    if ((b & 0xF0u) == 0xE0u)
        return 3;
    return 1; /* stray continuation byte: step one to stay live */
}

uint16_t TextInput::nextCell(uint16_t pos) const
{
    const uint16_t n = static_cast<uint16_t>(pos + cellLen(pos));
    return (n < len_) ? n : len_;
}

uint16_t TextInput::prevCell(uint16_t pos) const
{
    if (pos == 0)
        return 0;
    uint16_t i = static_cast<uint16_t>(pos - 1);
    /* Walk back over UTF-8 continuation bytes to the lead byte. */
    while ((i > 0) && ((static_cast<uint8_t>(buf_[i]) & 0xC0u) == 0x80u))
        i--;
    return i;
}

void TextInput::spliceCell(uint16_t pos, const char *enc, int n)
{
    /* Replace the cell at pos (its cellLen bytes) with the n bytes at enc,
     * shifting the tail and adjusting len_. No-op if it would not fit. */
    if ((buf_ == nullptr) || (pos > len_))
        return;
    const uint16_t old = cellLen(pos);
    if (static_cast<int>(len_) - old + n + 1 > cap_)
        return;
    memmove(buf_ + pos + n, buf_ + pos + old,
            static_cast<size_t>(len_ - pos - old) + 1); /* include NUL */
    for (int i = 0; i < n; i++)
        buf_[pos + i] = enc[i];
    len_ = static_cast<uint16_t>(len_ - old + n);
}

void TextInput::cycle(int dir)
{
    if ((buf_ == nullptr) || (cs_ == nullptr))
        return;
    if (cursor_ >= len_)
        return;

    /* The cycle is the character set plus a delete slot, reached from either
     * end, so every radio can delete; a move or commit applies it. */
    const int n = static_cast<int>(cs_->len);
    const int total = n + 1;
    const uint16_t clen = cellLen(cursor_);
    int idx = (buf_[cursor_] == DELETE_CELL) ? n :
              (clen == 1)                    ? cs_->indexOf(buf_[cursor_]) :
                            -1; /* multibyte cell: start fresh */
    idx = ((idx + dir) % total + total) % total;
    const char rep = (idx == n) ? DELETE_CELL : cs_->at(idx);
    if (clen == 1)
        buf_[cursor_] = rep; /* fast path, no length change */
    else
        spliceCell(cursor_, &rep,
                   1); /* collapse a multibyte cell to one byte */
}

void TextInput::deleteAt(uint16_t pos)
{
    if ((buf_ == nullptr) || (pos >= len_))
        return;
    const uint16_t clen = cellLen(pos);
    memmove(buf_ + pos, buf_ + pos + clen,
            static_cast<size_t>(len_ - pos - clen) + 1); /* incl NUL */
    len_ = static_cast<uint16_t>(len_ - clen);
    if (cursor_ > len_)
        cursor_ = len_;
    /* Keep the cursor on a cell. */
    if ((cursor_ >= len_) && (len_ > 0))
        cursor_ = prevCell(len_);
    seedBlank();
    tapActive_ = false;
}

void TextInput::resolvePendingDelete()
{
    if ((buf_ != nullptr) && (cursor_ < len_) && (buf_[cursor_] == DELETE_CELL))
        deleteAt(cursor_);
}

void TextInput::moveCursor(int dir)
{
    if (buf_ == nullptr)
        return;
    commitPending();
    if (slotMode_) {
        /* Move over editable slots only; separators are skipped by slotToPos. */
        if ((dir < 0) && (slotCursor_ > 0))
            slotCursor_--;
        else if ((dir > 0) && (slotCursor_ + 1 < slotCount_))
            slotCursor_++;
        syncSlotCursor();
        return;
    }
    /* A move key applied while sitting on the ⌫ slot performs the delete
     * (consuming the keypress) rather than moving. */
    if ((cursor_ < len_) && (buf_[cursor_] == DELETE_CELL)) {
        deleteAt(cursor_);
        return;
    }
    if (dir < 0) {
        if (cursor_ > 0)
            cursor_ = prevCell(cursor_);
    } else {
        const uint16_t nxt = nextCell(cursor_);
        if (nxt < len_) {
            cursor_ = nxt;
        } else if (len_ + 1 < cap_) {
            /* Extend with a blank and step onto it. */
            buf_[len_] = ' ';
            buf_[len_ + 1] = '\0';
            len_++;
            cursor_ = static_cast<uint16_t>(len_ - 1);
        }
    }
}

void TextInput::putCodePoint(uint32_t cp)
{
    /* Replace the cell under the cursor and leave the cursor on it, as a typed
     * character would. */
    if (buf_ == nullptr)
        return;
    char enc[3];
    const size_t n = utf8Encode(cp, enc);
    spliceCell(cursor_, enc, static_cast<int>(n));
}

void TextInput::backspace()
{
    if (buf_ == nullptr)
        return;
    if (slotMode_) {
        /* Clear the last filled slot and step back onto it. */
        if (slotCursor_ == 0)
            return;
        slotCursor_--;
        const int p = slotToPos(slotCursor_);
        if (p >= 0)
            buf_[p] = placeholder_;
        syncSlotCursor();
        return;
    }
    /* On the ⌫ slot, '*' deletes that slot; otherwise it backspaces. */
    if ((cursor_ < len_) && (buf_[cursor_] == DELETE_CELL)) {
        deleteAt(cursor_);
        return;
    }
    if ((cursor_ == 0) || (len_ == 0))
        return;
    const uint16_t prev = prevCell(cursor_);
    const uint16_t clen = static_cast<uint16_t>(cursor_ - prev);
    memmove(buf_ + prev, buf_ + cursor_,
            static_cast<size_t>(len_ - cursor_) + 1); /* include NUL */
    len_ = static_cast<uint16_t>(len_ - clen);
    cursor_ = prev;
    tapActive_ = false;
}

void TextInput::tapKey(uint8_t keyIndex, long long nowTick)
{
    if ((buf_ == nullptr) || (tapTable_ == nullptr)
        || (keyIndex >= ARRAY_SIZE(tapTable_->keys)))
        return;
    const char *str = tapTable_->keys[keyIndex];
    if ((str == nullptr) || (str[0] == '\0'))
        return; /* action / unused key — nothing to type */

    const size_t n = strlen(str);
    const bool cont = tapActive_
                   && (tapKeyIdx_ == static_cast<int8_t>(keyIndex))
                   && ((nowTick - tapTick_) < input_longPressTimeout);
    if (cont) {
        /* Same key within the window: cycle the character in place. */
        tapSet_ = static_cast<uint8_t>((tapSet_ + 1) % n);
    } else {
        /* The previous character is committed; advance to a fresh slot
         * (moveCursor extends at the end) and overwrite it. */
        if (tapActive_)
            moveCursor(+1);
        tapSet_ = 0;
    }
    buf_[cursor_] = str[tapSet_]; /* set the character under the cursor */
    tapActive_ = true;
    tapKeyIdx_ = static_cast<int8_t>(keyIndex);
    tapTick_ = nowTick;
}

void TextInput::commitPending()
{
    tapActive_ = false;
    tapKeyIdx_ = -1;
}

TextInput::KeyResult TextInput::commit()
{
    commitPending();
    if (!slotMode_)
        stripTrailingSpaces();
    return KeyResult::Committed;
}

char TextInput::slotChar(uint16_t ordinal) const
{
    const int d = slotDigit(ordinal);
    return (d >= 0) ? static_cast<char>('0' + d) : placeholder_;
}

void TextInput::handlePrefilledKey(uint32_t keys, bool &pristine,
                                   long long nowTick)
{
    if (pristine) {
        if ((keys & KBD_NUM_MASK) != 0u)
            clearSlots();
        pristine = false;
    }
    handleKey(keys, nowTick);
}

TextInput::KeyResult TextInput::handleKey(uint32_t keys, long long nowTick)
{
    if ((keys & KEY_ENTER) != 0u)
        return commit();
    if ((keys & KEY_ESC) != 0u)
        return KeyResult::Cancelled;
    if ((keys & KEY_STAR) != 0u) {
        backspace();
        return KeyResult::Editing;
    }
    if ((keys & KEY_HASH) != 0u) {
        if (slotMode_)
            return KeyResult::NotHandled; /* '#' is not a slot command */
        if (!kbdSpaceOnHash()) {
            clear();
            return KeyResult::Editing;
        }
    }
    /* A digit, or '#' where it is the space key, types through multi-tap. */
    const uint32_t chars = keys & (KBD_NUM_MASK | KEY_HASH);
    if (chars != 0u) {
        const uint8_t k = static_cast<uint8_t>(__builtin_ctz(chars));
        if (slotMode_)
            putDigit(k);
        else if (tapTable_ != nullptr)
            tapKey(k, nowTick);
        else
            return KeyResult::NotHandled;
        return KeyResult::Editing;
    }
    if ((keys & (KEY_LEFT | KEY_RIGHT)) != 0u) {
        moveCursor(((keys & KEY_RIGHT) != 0u) ? +1 : -1);
        return KeyResult::Editing;
    }
    if ((keys & (KEY_UP | KEY_DOWN)) != 0u) {
        if (slotMode_)
            return KeyResult::NotHandled; /* host owns UP/DOWN (e.g. field swap) */
        /* Without LEFT/RIGHT, UP/DOWN are what moves the cursor. */
        const int dir = ((keys & KEY_UP) != 0u) ? +1 : -1;
        if (kbdHasArrows())
            cycle(dir);
        else
            moveCursor(-dir);
        return KeyResult::Editing;
    }
    return KeyResult::NotHandled;
}

TextInput::KeyResult TextInput::handleEvent(const Event &e, long long nowTick)
{
    switch (e.kind) {
        case EvKind::Encoder:
            /* The knob changes the character where LEFT/RIGHT move the cursor
             * or nothing else can, and moves the cursor otherwise. */
            if (kbdHasArrows() || knobOnly())
                cycle(e.encoder);
            else
                moveCursor(e.encoder);
            return KeyResult::Editing;

        case EvKind::KeyRepeat:
            if ((e.keys & REPEAT_KEYS) == 0u)
                return KeyResult::NotHandled;
            return handleKey(e.keys & REPEAT_KEYS, nowTick);

        case EvKind::KeyLong:
            /* The press that opened the editor is not this field's to commit. */
            if (knobOnly() && enterDown_ && ((e.keys & KEY_ENTER) != 0u))
                return commit();
            return KeyResult::NotHandled;

        case EvKind::Key:
            enterDown_ = ((e.keys & KEY_ENTER) != 0u);
            if (knobOnly() && enterDown_) {
                moveCursor(+1);
                return KeyResult::Editing;
            }
            return handleKey(e.keys, nowTick);

        default:
            return KeyResult::NotHandled;
    }
}

void TextInput::clear()
{
    if (buf_ == nullptr)
        return;
    buf_[0] = '\0';
    begin();
}

void TextInput::stripTrailingSpaces()
{
    if (buf_ == nullptr)
        return;
    resolvePendingDelete(); /* a pending ⌫ at commit deletes its slot */
    while ((len_ > 0) && (buf_[len_ - 1] == ' '))
        buf_[--len_] = '\0';
    if (cursor_ > len_)
        cursor_ = len_;
}

void TextInput::formatBracketed(char *out, size_t sz) const
{
    composeWindow(out, sz, 0, len_);
}

void TextInput::composeWindow(char *out, size_t sz, uint16_t lo,
                              uint16_t hi) const
{
    static constexpr size_t ELLIPSIS_LEN = sizeof(SYMBOL_ELLIPSIS) - 1;
    char *o = out;
    size_t rem = sz;

    *o = '\0';
    if (lo > 0)
        append(o, rem, SYMBOL_ELLIPSIS, ELLIPSIS_LEN);
    for (uint16_t i = lo; i < hi; i = nextCell(i)) {
        const bool del = (buf_[i] == DELETE_CELL);
        const char *cell = del ? DELETE_GLYPH : (buf_ + i);
        const size_t n = del ? (sizeof(DELETE_GLYPH) - 1) : cellLen(i);
        const bool here = (i == cursor_);
        if ((here && !append(o, rem, "[", 1)) || !append(o, rem, cell, n)
            || (here && !append(o, rem, "]", 1)))
            return;
    }
    if (hi < len_)
        append(o, rem, SYMBOL_ELLIPSIS, ELLIPSIS_LEN);
}

void TextInput::formatWindow(char *out, size_t sz, uint16_t maxW,
                             fontSize_t font) const
{
    formatBracketed(out, sz);
    if (gfx_getTextWidth(font, out) <= maxW)
        return;

    /* Grow a [lo, hi) cell window outward from the cursor, alternately left and
     * right, while the composed window (with its ellipses) still fits, so the
     * cursor cell stays visible and the field scrolls with it. */
    uint16_t lo = cursor_;
    uint16_t hi = nextCell(lo);
    char cand[LINE_BUF];
    bool grew = true;
    while (grew) {
        grew = false;
        if (lo > 0) {
            const uint16_t l = prevCell(lo);
            composeWindow(cand, sizeof(cand), l, hi);
            if (gfx_getTextWidth(font, cand) <= maxW) {
                lo = l;
                grew = true;
            }
        }
        if (hi < len_) {
            const uint16_t h = nextCell(hi);
            composeWindow(cand, sizeof(cand), lo, h);
            if (gfx_getTextWidth(font, cand) <= maxW) {
                hi = h;
                grew = true;
            }
        }
    }
    composeWindow(out, sz, lo, hi);
}

/**
 * Find the end of a wrapped line: after its last space, or mid-word when one
 * word is too wide, and always at least one character on.
 *
 * @param buf: text.
 * @param start: first byte of the line.
 * @param len: text length.
 * @param maxW: line width in pixels.
 * @param font: font.
 * @return the byte after the line.
 */
static uint16_t lineEnd(const char *buf, uint16_t start, uint16_t len, int maxW,
                        fontSize_t font)
{
    if (start >= len)
        return start;
    uint16_t i = start;
    uint16_t lastSpace = start;
    bool haveSpace = false;
    while (i < len) {
        if (spanWidth(buf, start, static_cast<uint16_t>(i + 1), font) > maxW) {
            /* One glyph too wide: still advance by one to guarantee progress. */
            if (i == start)
                return static_cast<uint16_t>(start + 1);
            if (haveSpace)
                return static_cast<uint16_t>(lastSpace + 1);
            return i;
        }
        if (buf[i] == ' ') {
            lastSpace = i;
            haveSpace = true;
        }
        i++;
    }
    return len;
}

void TextInput::ensureCursorVisibleV(uint16_t cursorRow, uint16_t rows,
                                     uint16_t visRows)
{
    if (cursorRow < topRow_)
        topRow_ = cursorRow;
    else if (cursorRow >= topRow_ + visRows)
        topRow_ = static_cast<uint16_t>(cursorRow - visRows + 1);
    if (topRow_ + visRows > rows)
        topRow_ = (rows > visRows) ? static_cast<uint16_t>(rows - visRows) : 0;
}

void TextInput::draw(DrawCtx &d)
{
    if ((buf_ == nullptr) || area_.empty())
        return;

    const int16_t fh = static_cast<int16_t>(gfx_getFontHeight(font_));
    const int16_t lh = static_cast<int16_t>(fh + LINE_GAP);

    auto drawCursor = [&](const Rect &lineBox, uint16_t lineStart) {
        const uint16_t preW = spanWidth(buf_, lineStart, cursor_, font_);
        const int16_t cx = static_cast<int16_t>(area_.x + preW);

        /* Block over the whole cell (a picker-inserted accent is one cell). */
        const uint16_t clen = cellLen(cursor_);
        const bool isDel = (cursor_ < len_) && (buf_[cursor_] == DELETE_CELL);
        char t[5];
        uint16_t tn = (clen < sizeof(t)) ? clen : 0u;
        for (uint16_t i = 0; i < tn; i++)
            t[i] = buf_[cursor_ + i];
        t[tn] = '\0';
        const bool onChar = (cursor_ < len_) && !isDel
                         && !((clen == 1) && (buf_[cursor_] == ' '));
        const char *glyph = isDel ? DELETE_GLYPH : t;
        const bool visible = isDel || onChar;
        uint16_t cw = visible ? gfx_getTextWidth(font_, glyph) : 3u;
        if (cw < 2u)
            cw = 2u;
        const Rect cell = { cx, lineBox.y, cw, lineBox.h };
        d.fillRect(cell, Sem::Primary);
        if (visible)
            d.textInBox(cell, font_, TEXT_ALIGN_LEFT, Sem::OnPrimary, glyph);
    };

    if (mode_ == Mode::SingleLine) {
        /* A SingleLine field is as wide as its text; long text is MultiLine. */
        d.textInBox(area_, font_, TEXT_ALIGN_LEFT, Sem::OnSurface, buf_);
        drawCursor(area_, 0);
        return;
    }

    const int maxW = static_cast<int>(area_.w) - SCROLL_W - SCROLL_GAP;
    uint16_t visRows =
        static_cast<uint16_t>(area_.h / static_cast<uint16_t>(lh));
    if (visRows == 0)
        visRows = 1;

    /* Pass 1: count wrapped rows and locate the cursor's row. */
    uint16_t rows = 0, cursorRow = 0, pos = 0;
    while (true) {
        const uint16_t end = lineEnd(buf_, pos, len_, maxW, font_);
        if ((cursor_ >= pos) && (cursor_ < end))
            cursorRow = rows;
        else if ((end >= len_) && (cursor_ == len_))
            cursorRow = rows;
        rows++;
        if (end >= len_)
            break;
        pos = end;
    }
    ensureCursorVisibleV(cursorRow, rows, visRows);

    /* Pass 2: draw the visible rows and the cursor. */
    pos = 0;
    for (uint16_t r = 0;; r++) {
        const uint16_t end = lineEnd(buf_, pos, len_, maxW, font_);
        if ((r >= topRow_) && (r < topRow_ + visRows)) {
            const int16_t rowY =
                static_cast<int16_t>(area_.y + (r - topRow_) * lh);
            const Rect lineBox = { area_.x, rowY, area_.w,
                                   static_cast<uint16_t>(lh) };
            char tmp[LINE_BUF];
            uint16_t n = static_cast<uint16_t>(end - pos);
            if (n >= sizeof(tmp))
                n = sizeof(tmp) - 1;
            memcpy(tmp, buf_ + pos, n);
            tmp[n] = '\0';
            for (uint16_t j = 0; j < n; j++)
                if (tmp[j]
                    == DELETE_CELL) /* the ⌫ slot is drawn by the cursor */
                    tmp[j] = ' ';
            d.textInBox(lineBox, font_, TEXT_ALIGN_LEFT, Sem::OnSurface, tmp);
            if (r == cursorRow)
                drawCursor(lineBox, pos);
        }
        if (end >= len_)
            break;
        pos = end;
    }

    /* Scroll indicator: a track with a thumb. */
    if (rows > visRows) {
        const int16_t barX = static_cast<int16_t>(area_.right() - 1);
        const int trackH = static_cast<int>(area_.h);
        int thumbH = trackH * visRows / rows;
        if (thumbH < SCROLL_MIN_H)
            thumbH = SCROLL_MIN_H;
        const int16_t thumbY =
            static_cast<int16_t>(area_.y + trackH * topRow_ / rows);
        const Rect track = { barX, area_.y, SCROLL_W, area_.h };
        d.fillRect(track, Sem::Surface);
        const Rect thumb = { barX, thumbY, SCROLL_W,
                             static_cast<uint16_t>(thumbH) };
        d.fillRect(thumb, Sem::OnSurfaceMuted);
    }
}

} // namespace ortxui
