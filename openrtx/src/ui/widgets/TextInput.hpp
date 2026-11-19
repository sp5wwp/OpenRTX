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
 * Reusable text-input field over a caller-owned buffer.
 *
 * Two deterministic (golden-testable) input methods drive the same buffer, and
 * the host picks per keypad capability: cursor-cycle (UP/DOWN cycle the char
 * through the Charset, LEFT/RIGHT or knob move) for arrow/knob radios, and ETSI
 * phone-style multi-tap (tapKey(): a digit types a letter, repeats cycle, a
 * different key or a lapsed window commits and advances) for numeric keypads.
 * The same state drives two renderers:
 *  - `formatBracketed()` composes an inline "W1[A]W" string for editors that
 *    live inside another widget (a list row or the VFO channel line);
 *  - `draw()` paints the field into its own area for a modal, with a block
 *    cursor and, in MultiLine mode, word-wrap + vertical scroll for long text
 *    (M17 SMS is ~821 chars). `DrawCtx` does not enforce its clip, so drawing
 *    self-limits to whole lines within `area_`.
 *
 * Deletion works on every radio: the cursor wheel has a trailing ⌫ slot, and
 * cycling a character to it then moving/committing removes it (keypad radios
 * also get '*' as a backspace shortcut). `putCodePoint()` is the UTF-8 character
 * picker seam for accented/symbol code points not on the keyboard; it
 * overwrites the cell under the cursor, just like a typed character would.
 */
class TextInput : public Object
{
public:
    /** Constructor. */
    TextInput() = default;

    /** Destructor. */
    ~TextInput() = default;

    enum class Mode : uint8_t {
        SingleLine,
        MultiLine,
    };

    /**
     * Bind the field to `buf` (holds up to `cap`-1 chars + NUL) with charset
     * `cs`. `font` is used only by draw(). Call begin() next to seed the cursor.
     */
    void configure(char *buf, uint16_t cap, const Charset &cs, Mode mode,
                   fontSize_t font);

    /**
     * Configure a fixed-width masked field over `buf` (numeric slot entry:
     * frequency, time/date). `mask` is a template where '9' marks an editable
     * digit slot and every other character is a literal separator, shown
     * verbatim and skipped by the cursor (e.g. "999.999", "99/99/99 99:99:99").
     * Unfilled slots render as `placeholder`. The buffer is initialised to the
     * mask with every slot set to the placeholder; use this instead of
     * configure() (begin() is not needed). Callers read back digits via
     * slotDigit() (which returns -1 for an unfilled slot) rather than the raw
     * buffer string, and may bypass draw() with their own renderer.
     *
     * Unfilled trailing slots stay as placeholders, which a trailing-zero field
     * (frequency) reads as zero; a host that needs full entry (time/date)
     * checks filledSlots() == slotCount() before committing.
     */
    void configureSlots(char *buf, uint16_t cap, const char *mask,
                        char placeholder, fontSize_t font);

    /** Seed length/cursor from the current buffer; an empty buffer starts as a
     *  single editable blank so there is always a character under the cursor. */
    void begin();

    /**
     * Resume an edit of the bound buffer where an earlier one left off.
     *
     * @param cursor: cursor position, from cursor().
     * @param topRow: first visible wrapped row, from topRow().
     */
    void resume(uint16_t cursor, uint16_t topRow);

    /** Bind the ETSI multi-tap table for numeric-keypad entry (optional; only
     *  needed when the host routes digit keys to tapKey()). */
    void setMultiTap(const MultiTapTable &t)
    {
        tapTable_ = &t;
    }

    /* ---- Edit ops (driven by the host's key mapping) ---- */
    void cycle(int dir);            //< dial the cell under the cursor
    void moveCursor(int dir);       //< move the cursor by one cell
    void putCodePoint(uint32_t cp); //< picker seam: overwrite the cell
    void backspace();               //< delete the cell before the cursor
    void clear();                   //< empty the field to one editable blank
    void stripTrailingSpaces();

    /** ETSI multi-tap: press of keypad key `keyIndex` (0-9, *=10, #=11) at tick
     *  `nowTick`. Same key within the window cycles the character in place; a
     *  different key or a lapsed window commits and advances. */
    void tapKey(uint8_t keyIndex, long long nowTick);
    /** Finalize any in-progress multi-tap character (call before a cursor move
     *  or on commit) so the next tap starts a fresh character. */
    void commitPending();

    /* ---- Slot/mask mode ops (numeric fixed-width entry) ---- */
    void
    putDigit(uint8_t d); //< write 0-9 at the cursor slot, advance the cursor
    void setSlotDigit(uint16_t ordinal,
                      int d); //< pre-fill slot: d 0-9, or <0 = placeholder
    int slotDigit(uint16_t ordinal) const; //< 0-9, or -1 if the slot is empty
    uint16_t slotCount() const
    {
        return slotCount_;
    }
    uint16_t filledSlots() const; //< count of filled slots, left-contiguous
    void setCursorSlot(uint16_t ordinal); //< position the cursor on a slot

    /**
     * Get the active slot.
     *
     * @return the slot ordinal, slotCount() when past the last slot.
     */
    uint16_t cursorSlot() const
    {
        return slotCursor_;
    }
    void clearSlots(); //< reset every slot to the placeholder, cursor to slot 0

    /** What an event did to the field. */
    enum class KeyResult : uint8_t {
        NotHandled, ///< The host may act on the event
        Editing,    ///< The field changed or its cursor moved
        Committed,  ///< ENTER: the host stores the text
        Cancelled,  ///< ESC: the host restores the original
    };

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
     * move.
     *
     * @param e: event.
     * @param nowTick: current tick, for multi-tap.
     * @return what the event did.
     */
    KeyResult handleEvent(const Event &e, long long nowTick);

    /* ---- Accessors ---- */
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
    char cursorChar() const
    {
        return (buf_ != nullptr && cursor_ < len_) ? buf_[cursor_] : ' ';
    }

    /** Compose the buffer with the cursor character wrapped in brackets into
     *  `out` (SingleLine inline rendering). */
    void formatBracketed(char *out, size_t sz) const;

    /** Compose a cursor-visible window of the bracketed buffer that fits `maxW`
     *  pixels at `font`, with a leading/trailing ellipsis (…) where content is
     *  truncated. For inline SingleLine editors whose field is narrower than
     *  the full content; the window scrolls to keep the cursor visible. Falls
     *  back to the full bracketed string when it already fits. */
    void formatWindow(char *out, size_t sz, uint16_t maxW,
                      fontSize_t font) const;

    /* ---- Object ---- */
    void draw(DrawCtx &d) override;

private:
    /** Finish the edit: end multi-tap and drop trailing blanks. */
    KeyResult commit();

    void ensureCursorVisibleV(uint16_t cursorRow, uint16_t rows,
                              uint16_t visRows);
    /* Compose the bracketed buffer restricted to cells [lo, hi) into `out`,
     * with a leading ellipsis when text precedes `lo` and a trailing one when
     * text follows `hi`. */
    void composeWindow(char *out, size_t sz, uint16_t lo, uint16_t hi) const;
    void deleteAt(uint16_t pos); //< remove the whole cell at `pos`
    void resolvePendingDelete(); //< apply a cycled-to ⌫ delete at the cursor

    /** Put an editable blank in an empty field, so a cell is under the cursor. */
    void seedBlank();

    /* Slot mode: map a slot ordinal to its buffer position (the ord-th '9' in
     * the mask), or -1; keep the render cursor (cursor_) on the active slot. */
    int slotToPos(uint16_t ordinal) const;
    void syncSlotCursor();

    /* UTF-8 cell stepping: a "cell" is one code point (1-3 bytes), so the cursor
     * and edits move over whole characters, not raw bytes -- picker-inserted
     * accents (2-byte Latin-1) then navigate and delete atomically. */
    uint16_t cellLen(uint16_t pos) const;  //< bytes of the cell starting at pos
    uint16_t nextCell(uint16_t pos) const; //< start of the cell after pos
    uint16_t prevCell(uint16_t pos) const; //< start of the cell before pos
    void spliceCell(uint16_t pos, const char *enc,
                    int n);                //< replace cell w/ enc

    static constexpr uint16_t kMaxSlots = 16; ///< Most slots of a masked field

    char *buf_ = nullptr;
    uint16_t cap_ = 0;
    uint16_t len_ = 0;
    uint16_t cursor_ = 0;
    const Charset *cs_ = nullptr;
    Mode mode_ = Mode::SingleLine;
    fontSize_t font_ = FONT_SIZE_8PT;
    uint16_t topRow_ = 0; //< first visible wrapped row (MultiLine scroll)

    /* Multi-tap (ETSI keypad) state. */
    const MultiTapTable *tapTable_ = nullptr;
    bool tapActive_ = false; //< a character is mid-tap under the cursor
    uint8_t tapSet_ = 0;     //< index into the current key's cycle string
    int8_t tapKeyIdx_ = -1;  //< last keypad key tapped
    long long tapTick_ = 0;  //< tick of the last tap (for the window)

    /* Slot/mask mode state. Slot buffer positions are resolved once at configure
     * time (the mask is not retained, so it may be a temporary), so every slot
     * op is O(1) and there is no dangling-pointer hazard. */
    bool slotMode_ = false;
    char placeholder_ = '-';
    uint16_t slotCount_ = 0;  //< number of editable slots laid into the buffer
    uint16_t slotCursor_ = 0; //< active slot ordinal, in [0, slotCount_]
    std::array<uint16_t, kMaxSlots> slotPos_ = {}; //< buffer offset of each slot
};

} // namespace ortxui

#endif /* ORTX_UI_TEXTINPUT_HPP */
