/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/TextInput.hpp"
#include "base/Event.hpp"
#include "base/Keypad.hpp"
#include "render/DrawCtx.hpp"
#include "style/Symbols.hpp"
#include "widgets/List.hpp"
#include "core/input.h"
#include "core/utils.h"
#include "interfaces/keyboard.h"

#include <algorithm>
#include <cstring>

namespace ortxui
{

/** Longest wrapped line measured or copied at once. */
static constexpr uint16_t LINE_BUF = 128;

static constexpr int16_t LINE_GAP = 2;    ///< Pixels between wrapped lines
static constexpr uint16_t SCROLL_GAP = 1; ///< Gap between text and scroll bar

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
    textBuf = buf;
    capacity = cap;
    charset = &cs;
    editMode = mode;
    textFont = font;
    len = 0;
    cursorPos = 0;
    firstVisibleRow = 0;
    tapActive = false;
    tapKeyIdx = -1;
    enterDown = false;
    /* Drop any slot mask, so a reused field takes text. */
    slotMode = false;
    numSlots = 0;
    slotCursor = 0;
}

void TextInput::configureSlots(char *buf, uint16_t cap, const char *mask,
                               char placeholder, fontSize_t font)
{
    if ((buf == nullptr) || (cap == 0) || (mask == nullptr))
        return;

    textBuf = buf;
    capacity = cap;
    charset = nullptr;
    editMode = Mode::SingleLine;
    textFont = font;
    slotMode = true;
    placeholderChar = placeholder;
    tapActive = false;
    tapKeyIdx = -1;
    enterDown = false;

    /* Lay the mask into the buffer and note each slot's offset. A mask longer
     * than the buffer or MAX_SLOTS is cut short, which slotCount() shows. */
    numSlots = 0;
    uint16_t i = 0;
    for (; (mask[i] != '\0') && (i + 1 < capacity); i++) {
        if (mask[i] == '9') {
            if (numSlots >= MAX_SLOTS)
                break;
            textBuf[i] = placeholderChar;
            slotPos[numSlots++] = i;
        } else {
            textBuf[i] = mask[i];
        }
    }
    textBuf[i] = '\0';
    len = i;
    slotCursor = 0;
    syncSlotCursor();
}

int TextInput::slotToPos(uint16_t ordinal) const
{
    return (ordinal < numSlots) ? static_cast<int>(slotPos[ordinal]) : -1;
}

void TextInput::syncSlotCursor()
{
    uint16_t ord = slotCursor;
    if (ord >= numSlots)
        ord = (numSlots > 0) ? static_cast<uint16_t>(numSlots - 1) : 0;
    const int p = slotToPos(ord);
    cursorPos = (p >= 0) ? static_cast<uint16_t>(p) : 0;
}

void TextInput::putDigit(uint8_t d)
{
    if (!slotMode || (d > 9) || (slotCursor >= numSlots))
        return;
    const int p = slotToPos(slotCursor);
    if (p < 0)
        return;
    textBuf[p] = static_cast<char>('0' + d);
    slotCursor++;
    syncSlotCursor();
}

void TextInput::setSlotDigit(uint16_t ordinal, int d)
{
    if (!slotMode || (ordinal >= numSlots))
        return;
    const int p = slotToPos(ordinal);
    if (p < 0)
        return;
    /* An out-of-range digit clears the slot rather than wrapping. */
    textBuf[p] = ((d < 0) || (d > 9)) ? placeholderChar :
                                        static_cast<char>('0' + d);
}

int TextInput::slotDigit(uint16_t ordinal) const
{
    if (!slotMode || (ordinal >= numSlots))
        return -1;
    const int p = slotToPos(ordinal);
    if (p < 0)
        return -1;
    const char c = textBuf[p];
    return ((c >= '0') && (c <= '9')) ? (c - '0') : -1;
}

uint16_t TextInput::filledSlots() const
{
    uint16_t n = 0;
    while ((n < numSlots) && (slotDigit(n) >= 0))
        n++;
    return n;
}

void TextInput::setCursorSlot(uint16_t ordinal)
{
    if (!slotMode)
        return;
    slotCursor = (ordinal <= numSlots) ? ordinal : numSlots;
    syncSlotCursor();
}

void TextInput::clearSlots()
{
    if (!slotMode)
        return;
    for (uint16_t o = 0; o < numSlots; o++)
        textBuf[slotPos[o]] = placeholderChar;
    slotCursor = 0;
    syncSlotCursor();
}

void TextInput::begin()
{
    if ((textBuf == nullptr) || (capacity == 0))
        return;
    len = static_cast<uint16_t>(strnlen(textBuf, capacity - 1));
    textBuf[len] = '\0';
    cursorPos = 0;
    seedBlank();
    firstVisibleRow = 0;
    tapActive = false;
    tapKeyIdx = -1;
}

void TextInput::seedBlank()
{
    if ((len == 0) && (capacity >= 2)) {
        textBuf[0] = ' ';
        textBuf[1] = '\0';
        len = 1;
        cursorPos = 0;
    }
}

void TextInput::resume(uint16_t cursor, uint16_t topRow)
{
    begin();
    cursorPos = (cursor <= len) ? cursor : len;
    firstVisibleRow = topRow;
}

uint16_t TextInput::cellLen(uint16_t pos) const
{
    if ((textBuf == nullptr) || (pos >= len))
        return 0;
    const uint8_t b = static_cast<uint8_t>(textBuf[pos]);
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
    return (n < len) ? n : len;
}

uint16_t TextInput::prevCell(uint16_t pos) const
{
    if (pos == 0)
        return 0;
    uint16_t i = static_cast<uint16_t>(pos - 1);
    /* Walk back over UTF-8 continuation bytes to the lead byte. */
    while ((i > 0) && ((static_cast<uint8_t>(textBuf[i]) & 0xC0u) == 0x80u))
        i--;
    return i;
}

void TextInput::spliceCell(uint16_t pos, const char *enc, int n)
{
    if ((textBuf == nullptr) || (pos > len))
        return;
    const uint16_t old = cellLen(pos);
    if (static_cast<int>(len) - old + n + 1 > capacity)
        return;
    memmove(textBuf + pos + n, textBuf + pos + old,
            static_cast<size_t>(len - pos - old) + 1); /* include NUL */
    for (int i = 0; i < n; i++)
        textBuf[pos + i] = enc[i];
    len = static_cast<uint16_t>(len - old + n);
}

void TextInput::cycle(int dir)
{
    if ((textBuf == nullptr) || (charset == nullptr))
        return;
    if (cursorPos >= len)
        return;

    /* The cycle is the character set plus a delete slot, reached from either
     * end, so every radio can delete; a move or commit applies it. A
     * multibyte cell starts the cycle afresh. */
    const int n = static_cast<int>(charset->len);
    const int total = n + 1;
    const uint16_t clen = cellLen(cursorPos);
    int idx = (textBuf[cursorPos] == DELETE_CELL) ? n :
              (clen == 1) ? charset->indexOf(textBuf[cursorPos]) :
                            -1;
    idx = ((idx + dir) % total + total) % total;
    const char rep = (idx == n) ? DELETE_CELL : charset->at(idx);
    if (clen == 1)
        textBuf[cursorPos] = rep;
    else
        spliceCell(cursorPos, &rep, 1);
}

void TextInput::deleteAt(uint16_t pos)
{
    if ((textBuf == nullptr) || (pos >= len))
        return;
    const uint16_t clen = cellLen(pos);
    memmove(textBuf + pos, textBuf + pos + clen,
            static_cast<size_t>(len - pos - clen) + 1); /* include NUL */
    len = static_cast<uint16_t>(len - clen);
    cursorPos = std::min(cursorPos, len);
    /* Keep the cursor on a cell. */
    if ((cursorPos >= len) && (len > 0))
        cursorPos = prevCell(len);
    seedBlank();
    tapActive = false;
}

void TextInput::resolvePendingDelete()
{
    if ((textBuf != nullptr) && (cursorPos < len)
        && (textBuf[cursorPos] == DELETE_CELL))
        deleteAt(cursorPos);
}

void TextInput::moveCursor(int dir)
{
    if (textBuf == nullptr)
        return;
    commitPending();
    if (slotMode) {
        if ((dir < 0) && (slotCursor > 0))
            slotCursor--;
        else if ((dir > 0) && (slotCursor + 1 < numSlots))
            slotCursor++;
        syncSlotCursor();
        return;
    }
    /* A move off the delete slot applies it instead of moving. */
    if ((cursorPos < len) && (textBuf[cursorPos] == DELETE_CELL)) {
        deleteAt(cursorPos);
        return;
    }
    if (dir < 0) {
        if (cursorPos > 0)
            cursorPos = prevCell(cursorPos);
    } else {
        const uint16_t nxt = nextCell(cursorPos);
        if (nxt < len) {
            cursorPos = nxt;
        } else if (len + 1 < capacity) {
            /* Extend with a blank and step onto it. */
            textBuf[len] = ' ';
            textBuf[len + 1] = '\0';
            len++;
            cursorPos = static_cast<uint16_t>(len - 1);
        }
    }
}

void TextInput::putCodePoint(uint32_t cp)
{
    if (textBuf == nullptr)
        return;
    char enc[3];
    const size_t n = utf8Encode(cp, enc);
    spliceCell(cursorPos, enc, static_cast<int>(n));
}

void TextInput::backspace()
{
    if (textBuf == nullptr)
        return;
    if (slotMode) {
        /* Clear the last filled slot and step back onto it. */
        if (slotCursor == 0)
            return;
        slotCursor--;
        const int p = slotToPos(slotCursor);
        if (p >= 0)
            textBuf[p] = placeholderChar;
        syncSlotCursor();
        return;
    }
    /* On the delete slot, '*' deletes that slot; otherwise it backspaces. */
    if ((cursorPos < len) && (textBuf[cursorPos] == DELETE_CELL)) {
        deleteAt(cursorPos);
        return;
    }
    if ((cursorPos == 0) || (len == 0))
        return;
    const uint16_t prev = prevCell(cursorPos);
    const uint16_t clen = static_cast<uint16_t>(cursorPos - prev);
    memmove(textBuf + prev, textBuf + cursorPos,
            static_cast<size_t>(len - cursorPos) + 1); /* include NUL */
    len = static_cast<uint16_t>(len - clen);
    cursorPos = prev;
    tapActive = false;
}

void TextInput::tapKey(uint8_t keyIndex, long long nowTick)
{
    if ((textBuf == nullptr) || (tapTable == nullptr)
        || (keyIndex >= ARRAY_SIZE(tapTable->keys)))
        return;
    const char *str = tapTable->keys[keyIndex];
    if ((str == nullptr) || (str[0] == '\0'))
        return; /* the key types nothing */

    const size_t n = strlen(str);
    const bool cont = tapActive && (tapKeyIdx == static_cast<int8_t>(keyIndex))
                   && ((nowTick - tapTick) < input_longPressTimeout);
    if (cont) {
        /* Same key within the window: cycle the character in place. */
        tapSet = static_cast<uint8_t>((tapSet + 1) % n);
    } else {
        /* Keep the previous character and type on the next cell. */
        if (tapActive)
            moveCursor(+1);
        tapSet = 0;
    }
    textBuf[cursorPos] = str[tapSet];
    tapActive = true;
    tapKeyIdx = static_cast<int8_t>(keyIndex);
    tapTick = nowTick;
}

void TextInput::commitPending()
{
    tapActive = false;
    tapKeyIdx = -1;
}

TextInput::KeyResult TextInput::commit()
{
    commitPending();
    if (!slotMode)
        stripTrailingSpaces();
    return KeyResult::Committed;
}

char TextInput::slotChar(uint16_t ordinal) const
{
    const int d = slotDigit(ordinal);
    return (d >= 0) ? static_cast<char>('0' + d) : placeholderChar;
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
        if (slotMode)
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
        if (slotMode)
            putDigit(k);
        else if (tapTable != nullptr)
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
        if (slotMode)
            return KeyResult::NotHandled; /* the host owns UP/DOWN */
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
            /* The press that opened the editor does not commit it. */
            if (knobOnly() && enterDown && ((e.keys & KEY_ENTER) != 0u))
                return commit();
            return KeyResult::NotHandled;

        case EvKind::Key:
            enterDown = ((e.keys & KEY_ENTER) != 0u);
            if (knobOnly() && enterDown) {
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
    if (textBuf == nullptr)
        return;
    textBuf[0] = '\0';
    begin();
}

void TextInput::stripTrailingSpaces()
{
    if (textBuf == nullptr)
        return;
    resolvePendingDelete();
    while ((len > 0) && (textBuf[len - 1] == ' '))
        textBuf[--len] = '\0';
    cursorPos = std::min(cursorPos, len);
}

void TextInput::formatBracketed(char *out, size_t sz) const
{
    composeWindow(out, sz, 0, len);
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
        const bool del = (textBuf[i] == DELETE_CELL);
        const char *cell = del ? DELETE_GLYPH : (textBuf + i);
        const size_t n = del ? (sizeof(DELETE_GLYPH) - 1) : cellLen(i);
        const bool here = (i == cursorPos);
        if ((here && !append(o, rem, "[", 1)) || !append(o, rem, cell, n)
            || (here && !append(o, rem, "]", 1)))
            return;
    }
    if (hi < len)
        append(o, rem, SYMBOL_ELLIPSIS, ELLIPSIS_LEN);
}

void TextInput::formatWindow(char *out, size_t sz, uint16_t maxW,
                             fontSize_t font) const
{
    formatBracketed(out, sz);
    if (gfx_getTextWidth(font, out) <= maxW)
        return;

    /* Grow a window of cells out from the cursor while it fits, so the
     * cursor stays visible. */
    uint16_t lo = cursorPos;
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
        if (hi < len) {
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
            /* A glyph wider than the line gets a line of its own. */
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
    if (cursorRow < firstVisibleRow)
        firstVisibleRow = cursorRow;
    else if (cursorRow >= firstVisibleRow + visRows)
        firstVisibleRow = static_cast<uint16_t>(cursorRow - visRows + 1);
    if (firstVisibleRow + visRows > rows)
        firstVisibleRow = (rows > visRows) ?
                              static_cast<uint16_t>(rows - visRows) :
                              0;
}

void TextInput::draw(DrawCtx &d)
{
    if ((textBuf == nullptr) || bounds.empty())
        return;

    const int16_t fh = static_cast<int16_t>(gfx_getFontHeight(textFont));
    const int16_t lh = static_cast<int16_t>(fh + LINE_GAP);

    auto drawCursor = [&](const Rect &lineBox, uint16_t lineStart) {
        const uint16_t preW = spanWidth(textBuf, lineStart, cursorPos,
                                        textFont);
        const int16_t cx = static_cast<int16_t>(bounds.x + preW);

        /* Block over the whole cell (a picker-inserted accent is one cell). */
        const uint16_t clen = cellLen(cursorPos);
        const bool isDel = (cursorPos < len)
                        && (textBuf[cursorPos] == DELETE_CELL);
        char t[5];
        uint16_t tn = (clen < sizeof(t)) ? clen : 0u;
        for (uint16_t i = 0; i < tn; i++)
            t[i] = textBuf[cursorPos + i];
        t[tn] = '\0';
        const bool onChar = (cursorPos < len) && !isDel
                         && !((clen == 1) && (textBuf[cursorPos] == ' '));
        const char *glyph = isDel ? DELETE_GLYPH : t;
        const bool visible = isDel || onChar;
        const uint16_t textW = visible ? gfx_getTextWidth(textFont, glyph) : 3u;
        const uint16_t cw = std::max<uint16_t>(textW, 2u);
        const Rect cell = { cx, lineBox.y, cw, lineBox.h };
        d.fillRect(cell, Sem::Primary);
        if (visible)
            d.textInBox(cell, textFont, TEXT_ALIGN_LEFT, Sem::OnPrimary, glyph);
    };

    if (editMode == Mode::SingleLine) {
        /* A SingleLine field is as wide as its text; long text is MultiLine. */
        d.textInBox(bounds, textFont, TEXT_ALIGN_LEFT, Sem::OnSurface, textBuf);
        drawCursor(bounds, 0);
        return;
    }

    const int maxW = static_cast<int>(bounds.w) - List::SCROLL_BAR_W
                   - SCROLL_GAP;
    uint16_t visRows =
        static_cast<uint16_t>(bounds.h / static_cast<uint16_t>(lh));
    if (visRows == 0)
        visRows = 1;

    /* Pass 1: count wrapped rows and locate the cursor's row. */
    uint16_t rows = 0, cursorRow = 0, pos = 0;
    while (true) {
        const uint16_t end = lineEnd(textBuf, pos, len, maxW, textFont);
        if ((cursorPos >= pos) && (cursorPos < end))
            cursorRow = rows;
        else if ((end >= len) && (cursorPos == len))
            cursorRow = rows;
        rows++;
        if (end >= len)
            break;
        pos = end;
    }
    ensureCursorVisibleV(cursorRow, rows, visRows);

    /* Pass 2: draw the visible rows and the cursor. */
    pos = 0;
    for (uint16_t r = 0;; r++) {
        const uint16_t end = lineEnd(textBuf, pos, len, maxW, textFont);
        if ((r >= firstVisibleRow) && (r < firstVisibleRow + visRows)) {
            const int16_t rowY =
                static_cast<int16_t>(bounds.y + (r - firstVisibleRow) * lh);
            const Rect lineBox = { bounds.x, rowY, bounds.w,
                                   static_cast<uint16_t>(lh) };
            char tmp[LINE_BUF];
            uint16_t n = static_cast<uint16_t>(end - pos);
            if (n >= sizeof(tmp))
                n = sizeof(tmp) - 1;
            memcpy(tmp, textBuf + pos, n);
            tmp[n] = '\0';
            /* The cursor draws the delete slot. */
            for (uint16_t j = 0; j < n; j++)
                if (tmp[j] == DELETE_CELL)
                    tmp[j] = ' ';
            d.textInBox(lineBox, textFont, TEXT_ALIGN_LEFT, Sem::OnSurface,
                        tmp);
            if (r == cursorRow)
                drawCursor(lineBox, pos);
        }
        if (end >= len)
            break;
        pos = end;
    }

    if (rows > visRows)
        List::drawScrollBar(d, bounds, firstVisibleRow, visRows, rows);
}

} // namespace ortxui
