/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_TEXTINPUT_HPP
#define ORTX_UI_TEXTINPUT_HPP

#include <array>
#include <cstdint>
#include <cstddef>
#include "core/Event.hpp"
#include "core/Object.hpp"
#include "core/graphics.h"
#include "style/Charsets.hpp"

namespace ortxui
{

/**
 * A text field over a caller-owned buffer, edited in place.
 *
 * In text mode the character under the cursor is changed by cycling it through
 * a Charset or typed by multi-tap on a numeric keypad; the end of the cycle is a
 * delete slot, so every radio can delete. In slot mode the field is a fixed
 * mask of digit slots (frequency, offset, time). An inline editor shows it with
 * formatBracketed() or formatWindow(); a modal one draws it with draw(), which
 * wraps and scrolls a MultiLine field.
 */
class TextInput : public Object
{
public:
    /** Constructor. */
    TextInput() = default;

    /** Destructor. */
    ~TextInput() = default;

    /** Layout of a text field. */
    enum class Mode : uint8_t {
        SingleLine, ///< One line, as long as the field is wide
        MultiLine,  ///< Wrapped at words and scrolled to the cursor
    };

    /** What an event did to the field. */
    enum class KeyResult : uint8_t {
        NotHandled, ///< The host may act on the event
        Editing,    ///< The field changed or its cursor moved
        Committed,  ///< ENTER: the host stores the text
        Cancelled,  ///< ESC: the host restores the original
    };

    /**
     * Bind the field to a text buffer. Call begin() or resume() next.
     *
     * @param buf: buffer, holding up to `cap` - 1 characters and a NUL.
     * @param cap: buffer capacity.
     * @param cs: characters the cursor cycles through.
     * @param mode: layout.
     * @param font: font draw() uses.
     */
    void configure(char *buf, uint16_t cap, const Charset &cs, Mode mode,
                   fontSize_t font);

    /**
     * Bind the field to a buffer of digit slots, laid out from a mask where '9'
     * is a slot and any other character a separator the cursor skips (for
     * example "999.999"). Every slot starts as `placeholder`. Unfilled trailing
     * slots read as -1 from slotDigit(); a host that needs every slot checks
     * filledSlots() == slotCount() before it commits.
     *
     * @param buf: buffer, at least as long as the mask plus a NUL.
     * @param cap: buffer capacity.
     * @param mask: slot mask.
     * @param placeholder: character of an unfilled slot.
     * @param font: font draw() uses.
     */
    void configureSlots(char *buf, uint16_t cap, const char *mask,
                        char placeholder, fontSize_t font);

    /** Start editing the bound text, with the cursor on its first character. */
    void begin();

    /**
     * Resume an edit of the bound buffer where an earlier one left off.
     *
     * @param cursor: cursor position, from cursor().
     * @param topRow: first visible wrapped row, from topRow().
     */
    void resume(uint16_t cursor, uint16_t topRow);

    /**
     * Bind the multi-tap table digits type through.
     *
     * @param t: table.
     */
    void setMultiTap(const MultiTapTable &t)
    {
        tapTable_ = &t;
    }

    /**
     * Apply a key press. ENTER commits, ESC cancels, '*' deletes back, '#'
     * clears the field (in text mode, where it is not the space key) and digits
     * type through multi-tap or fill the next slot. LEFT/RIGHT move the cursor;
     * in text mode UP/DOWN change the character, or move the cursor on a keypad
     * without LEFT/RIGHT. UP/DOWN in slot mode are left to the host.
     *
     * @param keys: keyboard bitmask.
     * @param nowTick: current tick, for multi-tap.
     * @return what the key did.
     */
    KeyResult handleKey(uint32_t keys, long long nowTick);

    /**
     * Apply an event to a text field: a press as handleKey(), held arrows and
     * '*' as repeated presses, and the knob as a character change or a cursor
     * move. On a keypad with neither arrows nor digits, ENTER steps to the next
     * character and a long ENTER commits, if it was pressed after the field
     * was bound.
     *
     * @param e: event.
     * @param nowTick: current tick, for multi-tap.
     * @return what the event did.
     */
    KeyResult handleEvent(const Event &e, long long nowTick);

    /**
     * Replace the character under the cursor, leaving the cursor on it.
     *
     * @param cp: code point, from the character picker.
     */
    void putCodePoint(uint32_t cp);

    /**
     * Write a digit in the slot under the cursor and step to the next slot.
     *
     * @param d: digit, 0-9.
     */
    void putDigit(uint8_t d);

    /**
     * Fill a slot without moving the cursor.
     *
     * @param ordinal: slot.
     * @param d: digit 0-9, or any other value for the placeholder.
     */
    void setSlotDigit(uint16_t ordinal, int d);

    /**
     * Get the digit in a slot.
     *
     * @param ordinal: slot.
     * @return the digit, or -1 for an unfilled slot.
     */
    int slotDigit(uint16_t ordinal) const;

    /** Number of slots. */
    uint16_t slotCount() const
    {
        return slotCount_;
    }

    /** Number of filled slots from the first one on. */
    uint16_t filledSlots() const;

    /**
     * Put the cursor on a slot.
     *
     * @param ordinal: slot, or slotCount() for past the last one.
     */
    void setCursorSlot(uint16_t ordinal);

    /**
     * Get the active slot.
     *
     * @return the slot ordinal, slotCount() when past the last slot.
     */
    uint16_t cursorSlot() const
    {
        return slotCursor_;
    }

    /** Empty every slot and put the cursor on the first. */
    void clearSlots();

    /**
     * Get the character a slot shows.
     *
     * @param ordinal: slot.
     * @return the digit, or the placeholder if the slot is empty.
     */
    char slotChar(uint16_t ordinal) const;

    /**
     * Handle a key on a slot field pre-filled with a value: while `pristine`,
     * a digit first empties the field so it retypes the value, and the key
     * ends the pre-fill. Call it only for a key that edits the field.
     *
     * @param keys: keys pressed.
     * @param pristine: the field still holds its pre-filled value.
     * @param nowTick: time of the key press, in ms.
     */
    void handlePrefilledKey(uint32_t keys, bool &pristine, long long nowTick);

    /** Cursor position in the buffer. */
    uint16_t cursor() const
    {
        return cursor_;
    }

    /**
     * Get the first visible wrapped row.
     *
     * @return the row.
     */
    uint16_t topRow() const
    {
        return topRow_;
    }

    /** Character under the cursor, for voice prompts. */
    char cursorChar() const
    {
        return (buf_ != nullptr && cursor_ < len_) ? buf_[cursor_] : ' ';
    }

    /**
     * Compose the text with the cursor character in brackets, as "W1[A]W".
     *
     * @param out: output.
     * @param sz: output size.
     */
    void formatBracketed(char *out, size_t sz) const;

    /**
     * Compose the part of formatBracketed() around the cursor that fits a
     * width, with an ellipsis where text is cut off.
     *
     * @param out: output.
     * @param sz: output size.
     * @param maxW: width in pixels.
     * @param font: font the text is drawn in.
     */
    void formatWindow(char *out, size_t sz, uint16_t maxW,
                      fontSize_t font) const;

    /** Paint the text and a block cursor; a MultiLine field also scrolls. */
    void draw(DrawCtx &d) override;

private:
    /**
     * Change the character under the cursor to the next or previous one of the
     * character set, or to the delete slot after its last.
     *
     * @param dir: +1 or -1.
     */
    void cycle(int dir);

    /**
     * Move the cursor by one character, adding a blank past the end.
     *
     * @param dir: +1 or -1.
     */
    void moveCursor(int dir);

    /** Delete the character before the cursor. */
    void backspace();

    /** Empty the field to one editable blank. */
    void clear();

    /** Drop trailing blanks, applying a delete slot under the cursor. */
    void stripTrailingSpaces();

    /**
     * Type with multi-tap: the same key again within input_longPressTimeout
     * changes the character in place, any other press keeps it and advances.
     *
     * @param keyIndex: key, 0-9, '*' = 10, '#' = 11.
     * @param nowTick: current tick.
     */
    void tapKey(uint8_t keyIndex, long long nowTick);

    /** End a multi-tap, so the next tap starts a new character. */
    void commitPending();

    /** Finish the edit: end multi-tap and drop trailing blanks. */
    KeyResult commit();

    /**
     * Scroll so the cursor row is visible.
     *
     * @param cursorRow: wrapped row of the cursor.
     * @param rows: wrapped rows of the text.
     * @param visRows: rows the area shows.
     */
    void ensureCursorVisibleV(uint16_t cursorRow, uint16_t rows,
                              uint16_t visRows);

    /**
     * Compose characters [lo, hi) as formatBracketed() does, with an ellipsis
     * where text comes before or after them.
     *
     * @param out: output.
     * @param sz: output size.
     * @param lo: first character.
     * @param hi: character after the last.
     */
    void composeWindow(char *out, size_t sz, uint16_t lo, uint16_t hi) const;

    /**
     * Delete the character at a position.
     *
     * @param pos: position.
     */
    void deleteAt(uint16_t pos);

    /** Apply a delete slot under the cursor. */
    void resolvePendingDelete();

    /** Put an editable blank in an empty field, so a cell is under the cursor. */
    void seedBlank();

    /**
     * Get the buffer position of a slot.
     *
     * @param ordinal: slot.
     * @return the position, or -1 past the last slot.
     */
    int slotToPos(uint16_t ordinal) const;

    /** Keep the drawn cursor on the active slot, or the last one past it. */
    void syncSlotCursor();

    /**
     * Get the length of a character, so picked characters move and delete as
     * one.
     *
     * @param pos: position of its first byte.
     * @return its UTF-8 length in bytes.
     */
    uint16_t cellLen(uint16_t pos) const;

    /**
     * Get the start of the next character.
     *
     * @param pos: start of a character.
     * @return the start of the one after it, or the text length.
     */
    uint16_t nextCell(uint16_t pos) const;

    /**
     * Get the start of the previous character.
     *
     * @param pos: start of a character.
     * @return the start of the one before it.
     */
    uint16_t prevCell(uint16_t pos) const;

    /**
     * Replace a character with other bytes, if they fit.
     *
     * @param pos: start of the character.
     * @param enc: bytes.
     * @param n: number of bytes.
     */
    void spliceCell(uint16_t pos, const char *enc, int n);

    static constexpr uint16_t MAX_SLOTS = 16; ///< Most slots of a masked field

    long long tapTick_ = 0;                   ///< Tick of the last tap
    char *buf_ = nullptr;                     ///< Bound buffer
    const Charset *cs_ = nullptr;             ///< Characters to cycle through
    const MultiTapTable *tapTable_ = nullptr; ///< Characters digits type
    fontSize_t font_ = FONT_SIZE_8PT;         ///< Font draw() uses
    std::array<uint16_t, MAX_SLOTS> slotPos_ = {}; ///< Buffer offset of slots
    uint16_t cap_ = 0;                             ///< Buffer capacity
    uint16_t len_ = 0;                             ///< Text length in bytes
    uint16_t cursor_ = 0;                          ///< Cursor position
    uint16_t topRow_ = 0;                          ///< First visible row
    uint16_t slotCount_ = 0;                       ///< Number of slots
    uint16_t slotCursor_ = 0;                      ///< Active slot
    Mode mode_ = Mode::SingleLine;                 ///< Layout
    uint8_t tapSet_ = 0;     ///< Index into the tapped key's characters
    int8_t tapKeyIdx_ = -1;  ///< Last key tapped
    bool tapActive_ = false; ///< A character is being tapped
    bool enterDown_ = false; ///< ENTER went down while the field was bound
    bool slotMode_ = false;  ///< The field is a slot mask
    char placeholder_ = '-'; ///< Character of an unfilled slot
};

} // namespace ortxui

#endif /* ORTX_UI_TEXTINPUT_HPP */
