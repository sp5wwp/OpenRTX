/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_KEYPAD_HPP
#define ORTX_UI_KEYPAD_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include "hwconfig.h"

namespace ortxui
{

/*
 * The keypad keys are presence macros in the target's hwconfig.h; a target
 * without the keys leaves the macro undefined. The UI reads them only through
 * these functions.
 */

/**
 * Check whether the keypad has LEFT and RIGHT arrows. Without them the text
 * editors move the cursor with the knob or UP and DOWN.
 *
 * @return true if it has.
 */
constexpr bool kbdHasArrows()
{
#ifdef CONFIG_KBD_HAS_ARROWS
    return true;
#else
    return false;
#endif
}

/**
 * Check whether the keypad has number keys, which type text by multi-tap.
 *
 * @return true if it has.
 */
constexpr bool kbdHasNumeric()
{
#ifdef CONFIG_KBD_HAS_NUMERIC
    return true;
#else
    return false;
#endif
}

/**
 * Check whether the space key of the text editors is '#' rather than '0'.
 *
 * @return true for '#'.
 */
constexpr bool kbdSpaceOnHash()
{
#ifdef CONFIG_KBD_SPACE_ON_HASH
    return true;
#else
    return false;
#endif
}

} // namespace ortxui

#endif /* ORTX_UI_KEYPAD_HPP */
