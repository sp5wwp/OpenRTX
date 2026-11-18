/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/TextInput.hpp"
#include "render/DrawCtx.hpp"
#include "style/Symbols.hpp"
#include "interfaces/keyboard.h"

#include <cstdio>
#include <cstring>

namespace ortxui
{

namespace
{
/* Longest wrapped line we measure/copy at once (a screen row never approaches
 * this many glyphs). */
constexpr uint16_t kLineBuf = 128;

/* Multi-tap window: a same-key press within this many ms cycles the character
 * in place; longer commits it. Matches the classic input_longPressTimeout. */
constexpr long long kMultiTapMs = 700;

/* Sentinel stored in the buffer when the cursor character is cycled to the
 * "delete" slot in the wheel. It only ever exists at the cursor (transient) and
 * is resolved into an actual deletion on the next move / commit; it is rendered
 * as the ⌫ glyph. */
constexpr char kDelete = 0x7F;
constexpr char kDeleteGlyph[] = SYMBOL_BACKSPACE;

/* Minimal UTF-8 encoder (BMP) for insert(); built-in charsets are ASCII so the
 * one-byte path is what runs today. Returns the byte count. */
int utf8Encode(uint32_t cp, char out[4])
{
    if (cp < 0x80u) {
        out[0] = static_cast<char>(cp);
        return 1;
    }
    if (cp < 0x800u) {
        out[0] = static_cast<char>(0xC0u | (cp >> 6));
        out[1] = static_cast<char>(0x80u | (cp & 0x3Fu));
        return 2;
    }
    out[0] = static_cast<char>(0xE0u | (cp >> 12));
    out[1] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
    out[2] = static_cast<char>(0x80u | (cp & 0x3Fu));
    return 3;
}

/* Pixel width of buf[start, end) at the given font. */
uint16_t spanWidth(const char *buf, uint16_t start, uint16_t end,
                   fontSize_t font)
{
    char tmp[kLineBuf];
    uint16_t n = static_cast<uint16_t>(end - start);
    if (n >= sizeof(tmp))
        n = sizeof(tmp) - 1;
    memcpy(tmp, buf + start, n);
    tmp[n] = '\0';
    return gfx_getTextWidth(font, tmp);
}
} // namespace

void TextInput::configure(char *buf, uint16_t cap, const Charset &cs, Mode mode,
                          CursorStyle cursor, fontSize_t font)
{
    buf_ = buf;
    cap_ = cap;
    cs_ = &cs;
    mode_ = mode;
    cursorStyle_ = cursor;
    font_ = font;
    len_ = 0;
    cursor_ = 0;
    topRow_ = 0;
    tapActive_ = false;
    tapKeyIdx_ = -1;
    /* Leave any prior slot-mode state behind so a reused widget accepts text. */
    slotMode_ = false;
    slotCount_ = 0;
    slotCursor_ = 0;
}

void TextInput::configureSlots(char *buf, uint16_t cap, const char *mask,
                               char placeholder, CursorStyle cursor,
                               fontSize_t font)
{
    if ((buf == nullptr) || (cap == 0) || (mask == nullptr))
        return;

    buf_ = buf;
    cap_ = cap;
    cs_ = nullptr;
    mode_ = Mode::SingleLine;
    cursorStyle_ = cursor;
    font_ = font;
    editMode_ = EditMode::Overtype;
    slotMode_ = true;
    placeholder_ = placeholder;
    tapActive_ = false;
    tapKeyIdx_ = -1;

    /* Lay the mask into the buffer, every editable slot set to the placeholder,
     * and cache each slot's buffer offset so later ops don't rescan the mask.
     * The whole mask must fit in the buffer (plus a NUL) and in kMaxSlots; a
     * mask that overruns either bound is a caller sizing error -- lay down what
     * fits so nothing is silently corrupted, but the caller-visible slotCount()
     * then reveals the shortfall rather than a wrong value being committed. */
    slotCount_ = 0;
    uint16_t i = 0;
    for (; (mask[i] != '\0') && (i + 1 < cap_); i++) {
        if (mask[i] == '9') {
            if (slotCount_ >= kMaxSlots)
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
    if (len_ == 0) { /* always keep one editable blank under the cursor */
        buf_[0] = ' ';
        buf_[1] = '\0';
        len_ = 1;
    }
    cursor_ = 0;
    topRow_ = 0;
    tapActive_ = false;
    tapKeyIdx_ = -1;
}

uint16_t TextInput::cellLen(uint16_t pos) const
{
    if ((buf_ == nullptr) || (pos >= len_))
        return 0;
    const uint8_t b = static_cast<uint8_t>(buf_[pos]);
    if ((b & 0x80u) == 0x00u)
        return 1; /* ASCII / kDelete */
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

void TextInput::insertBlankAt(uint16_t pos)
{
    if ((buf_ == nullptr) || (pos > len_) || (len_ + 1 >= cap_))
        return;
    memmove(buf_ + pos + 1, buf_ + pos,
            static_cast<size_t>(len_ - pos) + 1); /* include NUL */
    buf_[pos] = ' ';
    len_++;
}

void TextInput::setEditMode(EditMode m)
{
    editMode_ = m;
    insertPending_ = false;
    /* Overtype always sits on a cell; if the caret was at the end, pull it back
     * onto the last character. */
    if ((m == EditMode::Overtype) && (cursor_ >= len_) && (len_ > 0))
        cursor_ = prevCell(len_);
}

void TextInput::cycle(int dir)
{
    if ((buf_ == nullptr) || (cs_ == nullptr))
        return;
    /* Insert mode: the caret sits between cells; the first dial opens a fresh
     * blank cell here (pushing text right) and then dials it, subsequent dials
     * keep editing that same cell. Moving the caret commits it. */
    if (editMode_ == EditMode::Insert) {
        if (!insertPending_) {
            if (len_ + 1 >= cap_)
                return;
            insertBlankAt(cursor_);
            insertPending_ = true;
        }
    }
    /* Cycling on an empty field starts a fresh editable blank. */
    if (len_ == 0) {
        if (cap_ < 2)
            return;
        buf_[0] = ' ';
        buf_[1] = '\0';
        len_ = 1;
        cursor_ = 0;
    }
    if (cursor_ >= len_)
        return;

    /* The wheel is the charset plus one trailing "delete" slot (rendered ⌫).
     * Landing on it marks the character for deletion; a move or commit applies
     * it. Reachable from either end (cycle down from blank, or up past the last
     * symbol), so it's visible on every radio without a dedicated key. */
    const int n = static_cast<int>(cs_->len);
    const int total = n + 1;
    const uint16_t clen = cellLen(cursor_);
    int idx = (buf_[cursor_] == kDelete) ? n :
              (clen == 1)                ? cs_->indexOf(buf_[cursor_]) :
                                           -1; /* multibyte cell: start fresh */
    idx = ((idx + dir) % total + total) % total;
    const char rep = (idx == n) ? kDelete : cs_->at(idx);
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
    /* Overtype keeps the cursor on a cell; Insert may rest as a caret at len_. */
    if ((editMode_ == EditMode::Overtype) && (cursor_ >= len_) && (len_ > 0))
        cursor_ = prevCell(len_);
    if (len_ == 0)
        cursor_ = 0;
    tapActive_ = false;
    insertPending_ = false;
}

void TextInput::resolvePendingDelete()
{
    if ((buf_ != nullptr) && (cursor_ < len_) && (buf_[cursor_] == kDelete))
        deleteAt(cursor_);
}

void TextInput::moveCursor(int dir)
{
    if (buf_ == nullptr)
        return;
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
    if ((cursor_ < len_) && (buf_[cursor_] == kDelete)) {
        deleteAt(cursor_);
        return;
    }
    /* Leaving a freshly inserted cell commits it (it is already in the buffer);
     * the caret then floats between committed cells again. */
    insertPending_ = false;
    if (dir < 0) {
        if (cursor_ > 0)
            cursor_ = prevCell(cursor_);
    } else {
        const uint16_t nxt = nextCell(cursor_);
        if (nxt < len_ || (cursor_ < len_ && nxt == len_)) {
            /* Insert mode allows the caret to rest just past the last cell; in
             * Overtype we keep the cursor on a cell and extend to add one. */
            cursor_ = (editMode_ == EditMode::Insert) ? nxt :
                      (nxt < len_)                    ? nxt :
                                                        cursor_;
        }
        if ((editMode_ == EditMode::Overtype) && (nxt >= len_)
            && (len_ + 1 < cap_)) {
            /* Extend with a blank and step onto it. */
            buf_[len_] = ' ';
            buf_[len_ + 1] = '\0';
            len_++;
            cursor_ = static_cast<uint16_t>(len_ - 1);
        }
    }
}

void TextInput::insert(uint32_t cp)
{
    if (buf_ == nullptr)
        return;
    char enc[4];
    const int n = utf8Encode(cp, enc);
    if (len_ + n + 1 > cap_) /* no room for the bytes plus the NUL */
        return;
    memmove(buf_ + cursor_ + n, buf_ + cursor_,
            static_cast<size_t>(len_ - cursor_) + 1); /* include NUL */
    for (int i = 0; i < n; i++)
        buf_[cursor_ + i] = enc[i];
    len_ = static_cast<uint16_t>(len_ + n);
    cursor_ = static_cast<uint16_t>(cursor_ + n);
    insertPending_ = false;
}

void TextInput::putCodePoint(uint32_t cp)
{
    /* The character picker respects the edit mode so it lands exactly like a
     * typed character: Insert pushes text right at the caret (advancing past
     * the new glyph); Overtype replaces the cell under the cursor in place and
     * leaves the cursor on it -- seeding an empty field first if needed. */
    if (buf_ == nullptr)
        return;
    if (editMode_ == EditMode::Insert) {
        insert(cp);
        return;
    }
    if (len_ == 0) {
        if (cap_ < 2)
            return;
        buf_[0] = ' ';
        buf_[1] = '\0';
        len_ = 1;
        cursor_ = 0;
    }
    char enc[4];
    const int n = utf8Encode(cp, enc);
    spliceCell(cursor_, enc, n);
    insertPending_ = false;
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
    if ((cursor_ < len_) && (buf_[cursor_] == kDelete)) {
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
    insertPending_ = false;
}

void TextInput::tapKey(uint8_t keyIndex, long long nowTick)
{
    if ((buf_ == nullptr) || (tapTable_ == nullptr) || (keyIndex >= 12))
        return;
    const char *str = tapTable_->keys[keyIndex];
    if ((str == nullptr) || (str[0] == '\0'))
        return; /* action / unused key — nothing to type */

    const size_t n = strlen(str);
    const bool cont = tapActive_
                   && (tapKeyIdx_ == static_cast<int8_t>(keyIndex))
                   && ((nowTick - tapTick_) < kMultiTapMs);
    if (cont) {
        /* Same key within the window: cycle the character in place. */
        tapSet_ = static_cast<uint8_t>((tapSet_ + 1) % n);
    } else if (editMode_ == EditMode::Insert) {
        /* Insert: commit the previous tap (step the caret past it) and open a
         * fresh cell at the caret, pushing the rest of the text right. */
        if (tapActive_)
            cursor_ = nextCell(cursor_);
        insertPending_ = false;
        insertBlankAt(cursor_);
        tapSet_ = 0;
    } else {
        /* Overtype: the previous character is committed; advance to a fresh
         * slot (moveCursor extends at the end) and overwrite it. */
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

TextInput::KeyResult TextInput::handleKey(uint32_t keys, long long nowTick)
{
    if ((keys & KEY_ENTER) != 0u) {
        commitPending();
        if ((validate_ != nullptr) && !validate_(hookCtx_))
            return KeyResult::Editing; /* rejected: keep editing, host shows why */
        if (!slotMode_)
            stripTrailingSpaces();
        if (onCommit_ != nullptr)
            onCommit_(hookCtx_);
        return KeyResult::Committed;
    }
    if ((keys & KEY_ESC) != 0u) {
        if (onCancel_ != nullptr)
            onCancel_(hookCtx_);
        return KeyResult::Cancelled;
    }
    if ((keys & KEY_STAR) != 0u) {
        backspace();
        return KeyResult::Editing;
    }
    if ((keys & KEY_HASH) != 0u) {
        if (slotMode_)
            return KeyResult::NotHandled; /* '#' is not a slot command */
        clear();
        return KeyResult::Editing;
    }
    const uint32_t digits = keys & KBD_NUM_MASK;
    if (digits != 0u) {
        const uint8_t d = static_cast<uint8_t>(__builtin_ctz(digits));
        if (slotMode_)
            putDigit(d);
        else if (tapTable_ != nullptr)
            tapKey(d, nowTick);
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
        cycle(((keys & KEY_UP) != 0u) ? +1 : -1);
        return KeyResult::Editing;
    }
    return KeyResult::NotHandled;
}

void TextInput::clear()
{
    if (buf_ == nullptr)
        return;
    buf_[0] = '\0';
    len_ = 0;
    cursor_ = 0;
    topRow_ = 0;
    tapActive_ = false;
    tapKeyIdx_ = -1;
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
    if ((out == nullptr) || (sz == 0))
        return;
    char *o = out;
    size_t rem = sz;
    if (buf_ != nullptr) {
        for (uint16_t i = 0; (i < len_) && (rem > 6); i++) {
            int n;
            if (i == cursor_)
                n = (buf_[i] == kDelete) ?
                        snprintf(o, rem, "[%s]", kDeleteGlyph) :
                        snprintf(o, rem, "[%c]", buf_[i]);
            else
                n = snprintf(o, rem, "%c", buf_[i]);
            if (n < 0)
                break;
            o += n;
            rem -= static_cast<size_t>(n);
        }
    }
    *o = '\0';
}

void TextInput::composeWindow(char *out, size_t sz, uint16_t lo,
                              uint16_t hi) const
{
    if ((out == nullptr) || (sz == 0))
        return;
    char *o = out;
    size_t rem = sz;
    int n;

    if ((lo > 0) && (rem > sizeof(SYMBOL_ELLIPSIS))) {
        n = snprintf(o, rem, "%s", SYMBOL_ELLIPSIS);
        if (n > 0) {
            o += n;
            rem -= static_cast<size_t>(n);
        }
    }
    if (buf_ != nullptr) {
        for (uint16_t i = lo; (i < hi) && (rem > 6); i++) {
            if (i == cursor_)
                n = (buf_[i] == kDelete) ?
                        snprintf(o, rem, "[%s]", kDeleteGlyph) :
                        snprintf(o, rem, "[%c]", buf_[i]);
            else
                n = snprintf(o, rem, "%c", buf_[i]);
            if (n < 0)
                break;
            o += n;
            rem -= static_cast<size_t>(n);
        }
    }
    if ((hi < len_) && (rem > sizeof(SYMBOL_ELLIPSIS))) {
        n = snprintf(o, rem, "%s", SYMBOL_ELLIPSIS);
        if (n > 0) {
            o += n;
            rem -= static_cast<size_t>(n);
        }
    }
    *o = '\0';
}

void TextInput::formatWindow(char *out, size_t sz, uint16_t maxW,
                             fontSize_t font) const
{
    if ((out == nullptr) || (sz == 0))
        return;

    /* Fast path: the whole bracketed string already fits. */
    formatBracketed(out, sz);
    if ((buf_ == nullptr) || (len_ == 0)
        || (gfx_getTextWidth(font, out) <= maxW))
        return;

    /* Grow a [lo, hi) cell window outward from the cursor, alternately left and
     * right, committing an expansion only while the composed window (including
     * its ellipses) still fits -- so the cursor cell stays visible and the
     * field scrolls with it. */
    uint16_t lo = (cursor_ < len_) ? cursor_ : static_cast<uint16_t>(len_ - 1);
    uint16_t hi = static_cast<uint16_t>(lo + 1);

    char cand[kLineBuf];
    bool grew = true;
    while (grew) {
        grew = false;
        if (lo > 0) {
            composeWindow(cand, sizeof(cand), static_cast<uint16_t>(lo - 1),
                          hi);
            if (gfx_getTextWidth(font, cand) <= maxW) {
                lo--;
                grew = true;
            }
        }
        if (hi < len_) {
            composeWindow(cand, sizeof(cand), lo,
                          static_cast<uint16_t>(hi + 1));
            if (gfx_getTextWidth(font, cand) <= maxW) {
                hi++;
                grew = true;
            }
        }
    }
    composeWindow(out, sz, lo, hi);
}

/* Exclusive end index of the wrapped line starting at `start` that fits `maxW`
 * pixels: break at the last space (word wrap), or mid-word when a single word
 * is too wide; always advances at least one character. */
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
    const int16_t lh = static_cast<int16_t>(fh + 2);

    auto drawCursor = [&](const Rect &lineBox, uint16_t lineStart) {
        const uint16_t preW = spanWidth(buf_, lineStart, cursor_, font_);
        const int16_t cx = static_cast<int16_t>(area_.x + preW);

        /* Insert mode shows a thin caret *between* cells, except while a fresh
         * cell is being dialed/tapped -- then the composing cell gets the block,
         * same as overtype. */
        const bool composing = insertPending_ || tapActive_;
        if ((editMode_ == EditMode::Insert) && !composing) {
            const Rect bar = { cx, lineBox.y, 2u, lineBox.h };
            d.fillRect(bar, Sem::Primary);
            return;
        }

        /* Block over the whole cell (a picker-inserted accent is one cell). */
        const uint16_t clen = cellLen(cursor_);
        const bool isDel = (cursor_ < len_) && (buf_[cursor_] == kDelete);
        char t[5];
        uint16_t tn = (clen < sizeof(t)) ? clen : 0u;
        for (uint16_t i = 0; i < tn; i++)
            t[i] = buf_[cursor_ + i];
        t[tn] = '\0';
        const bool onChar = (cursor_ < len_) && !isDel
                         && !((clen == 1) && (buf_[cursor_] == ' '));
        const char *glyph = isDel ? kDeleteGlyph : t;
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
        /* Assumes the content fits the field width (callsign/dest-length use);
         * long text uses MultiLine. */
        d.textInBox(area_, font_, TEXT_ALIGN_LEFT, Sem::OnSurface, buf_);
        drawCursor(area_, 0);
        return;
    }

    const int maxW = static_cast<int>(area_.w) - 3;
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
            char tmp[kLineBuf];
            uint16_t n = static_cast<uint16_t>(end - pos);
            if (n >= sizeof(tmp))
                n = sizeof(tmp) - 1;
            memcpy(tmp, buf_ + pos, n);
            tmp[n] = '\0';
            for (uint16_t j = 0; j < n; j++)
                if (tmp[j] == kDelete) /* the ⌫ slot is drawn by the cursor */
                    tmp[j] = ' ';
            d.textInBox(lineBox, font_, TEXT_ALIGN_LEFT, Sem::OnSurface, tmp);
            if (r == cursorRow)
                drawCursor(lineBox, pos);
        }
        if (end >= len_)
            break;
        pos = end;
    }

    /* Scroll indicator (mirrors List): a track with a thumb. */
    if (rows > visRows) {
        const int16_t barX = static_cast<int16_t>(area_.right() - 1);
        const int trackH = static_cast<int>(area_.h);
        int thumbH = trackH * visRows / rows;
        if (thumbH < 2)
            thumbH = 2;
        const int16_t thumbY =
            static_cast<int16_t>(area_.y + trackH * topRow_ / rows);
        const Rect track = { barX, area_.y, 2, area_.h };
        d.fillRect(track, Sem::Surface);
        const Rect thumb = { barX, thumbY, 2, static_cast<uint16_t>(thumbH) };
        d.fillRect(thumb, Sem::OnSurfaceMuted);
    }
}

} // namespace ortxui
