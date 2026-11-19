/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_SCRATCH_HPP
#define ORTX_UI_SCRATCH_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include "widgets/BarRow.hpp"
#include "widgets/SettingRow.hpp"

namespace ortxui
{

/**
 * Storage for a widget that is set up, used and forgotten within one call:
 * a list row, a macro tile, the volume HUD's bar. Only one view draws at a
 * time and none of them keeps the widget between calls, so one buffer serves
 * them all instead of each keeping its own.
 */
class Scratch
{
public:
    /** Constructor. */
    Scratch() = default;

    /** Destructor. */
    ~Scratch() = default;

    /** Bytes of the largest widget drawn through the scratch. */
    static constexpr size_t SIZE = std::max({
        sizeof(SettingRow),
        sizeof(BarRow),
    });

    /** Strictest alignment of the widgets. */
    static constexpr size_t ALIGN = std::max({
        alignof(SettingRow),
        alignof(BarRow),
    });

    /**
     * Get the scratch widget as a T, building a fresh one if it holds
     * another type. It stays valid until the next call asks for another type.
     *
     * @return the widget.
     */
    template <class T>
    static T &get()
    {
        static_assert(sizeof(T) <= SIZE, "widget larger than the scratch");
        static_assert(alignof(T) <= ALIGN, "widget alignment too strict");

        if (type != typeOf<T>()) {
            release();
            new (storage) T();
            type = typeOf<T>();
            destroy = destroyAs<T>;
        }

        return *reinterpret_cast<T *>(storage);
    }

private:
    /** Destroy the widget held, if any. */
    static void release();

    /**
     * Get the tag identifying a widget type.
     *
     * @return the tag: one distinct object per type.
     */
    template <class T>
    static const void *typeOf()
    {
        static char tag;
        return &tag;
    }

    /**
     * Destroy a widget held in the storage.
     *
     * @param p: the widget.
     */
    template <class T>
    static void destroyAs(void *p)
    {
        static_cast<T *>(p)->~T();
    }

    alignas(ALIGN) static uint8_t storage[SIZE]; ///< Where the widget lives
    static const void *type;                     ///< Type held, nullptr if none
    static void (*destroy)(void *p);             ///< Destroys the widget held
};

} // namespace ortxui

#endif /* ORTX_UI_SCRATCH_HPP */
