/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Embed the fonts of openrtx/fonts/ in flash, one blob per fontSize_t; paths
 * are relative to the build directory, as in voicePromptData.S. A C file
 * rather than a .S, so that it can read CONFIG_FONT_AA from hwconfig.h.
 */

#include "hwconfig.h"

/*
 * Mach-O, for the emulator built on macOS, names read-only data __TEXT,__const
 * and prefixes C symbols with an underscore.
 */
#ifdef __APPLE__
#define BLOB_SECTION ".pushsection __TEXT,__const\n"
#define BLOB_SYM(sym) "_" sym
#else
#define BLOB_SECTION ".pushsection .rodata\n"
#define BLOB_SYM(sym) sym
#endif

/**
 * Assembly embedding a font blob as <sym>_start/_end.
 *
 * @param sym: symbol prefix, as C names it.
 * @param path: blob file, relative to the build directory.
 */
/* The formatter splits the concatenated strings mid-name. */
/* clang-format off */
#define BLOB(sym, path)                        \
    BLOB_SECTION                               \
    ".global " BLOB_SYM(sym) "_start\n"        \
    ".global " BLOB_SYM(sym) "_end\n"          \
    BLOB_SYM(sym) "_start:\n"                  \
    "    .incbin \"" path "\"\n"               \
    ".set " BLOB_SYM(sym) "_end, .\n"          \
    ".popsection\n"
/* clang-format on */

/**
 * Assembly embedding a text font blob as _font_<name>_start/_end.
 *
 * @param name: blob name.
 * @param path: blob file, relative to the build directory.
 */
#define FONT_BLOB(name, path) BLOB("_font_" #name, path)

/**
 * Assembly embedding an icon font blob, the fallback of the same-size text
 * font, as _icons_<name>_start/_end.
 *
 * @param name: blob name.
 * @param path: blob file, relative to the build directory.
 */
#define ICON_BLOB(name, path) BLOB("_icons_" #name, path)

#ifdef CONFIG_FONT_AA
__asm__(FONT_BLOB(5, "../openrtx/fonts/ubuntu_5_4.bin"));
__asm__(FONT_BLOB(6, "../openrtx/fonts/ubuntu_6_4.bin"));
__asm__(FONT_BLOB(8, "../openrtx/fonts/ubuntu_8_4.bin"));
__asm__(FONT_BLOB(9, "../openrtx/fonts/ubuntu_9_4.bin"));
__asm__(FONT_BLOB(10, "../openrtx/fonts/ubuntu_10_4.bin"));
__asm__(FONT_BLOB(12, "../openrtx/fonts/ubuntu_12_4.bin"));
__asm__(FONT_BLOB(16, "../openrtx/fonts/ubuntu_16_4.bin"));

__asm__(ICON_BLOB(5, "../openrtx/fonts/icons_5_4.bin"));
__asm__(ICON_BLOB(6, "../openrtx/fonts/icons_6_4.bin"));
__asm__(ICON_BLOB(8, "../openrtx/fonts/icons_8_4.bin"));
__asm__(ICON_BLOB(9, "../openrtx/fonts/icons_9_4.bin"));
__asm__(ICON_BLOB(10, "../openrtx/fonts/icons_10_4.bin"));
__asm__(ICON_BLOB(12, "../openrtx/fonts/icons_12_4.bin"));
__asm__(ICON_BLOB(16, "../openrtx/fonts/icons_16_4.bin"));

/* Colour targets also embed the 1bpp set, for crisp text. */
__asm__(FONT_BLOB(crisp_5, "../openrtx/fonts/ubuntu_5_1.bin"));
__asm__(FONT_BLOB(crisp_6, "../openrtx/fonts/ubuntu_6_1.bin"));
__asm__(FONT_BLOB(crisp_8, "../openrtx/fonts/ubuntu_8_1.bin"));
__asm__(FONT_BLOB(crisp_9, "../openrtx/fonts/ubuntu_9_1.bin"));
__asm__(FONT_BLOB(crisp_10, "../openrtx/fonts/ubuntu_10_1.bin"));
__asm__(FONT_BLOB(crisp_12, "../openrtx/fonts/ubuntu_12_1.bin"));
__asm__(FONT_BLOB(crisp_16, "../openrtx/fonts/ubuntu_16_1.bin"));
#else
__asm__(FONT_BLOB(5, "../openrtx/fonts/ubuntu_5_1.bin"));
__asm__(FONT_BLOB(6, "../openrtx/fonts/ubuntu_6_1.bin"));
__asm__(FONT_BLOB(8, "../openrtx/fonts/ubuntu_8_1.bin"));
__asm__(FONT_BLOB(9, "../openrtx/fonts/ubuntu_9_1.bin"));
__asm__(FONT_BLOB(10, "../openrtx/fonts/ubuntu_10_1.bin"));
__asm__(FONT_BLOB(12, "../openrtx/fonts/ubuntu_12_1.bin"));
__asm__(FONT_BLOB(16, "../openrtx/fonts/ubuntu_16_1.bin"));

__asm__(ICON_BLOB(5, "../openrtx/fonts/icons_5_1.bin"));
__asm__(ICON_BLOB(6, "../openrtx/fonts/icons_6_1.bin"));
__asm__(ICON_BLOB(8, "../openrtx/fonts/icons_8_1.bin"));
__asm__(ICON_BLOB(9, "../openrtx/fonts/icons_9_1.bin"));
__asm__(ICON_BLOB(10, "../openrtx/fonts/icons_10_1.bin"));
__asm__(ICON_BLOB(12, "../openrtx/fonts/icons_12_1.bin"));
__asm__(ICON_BLOB(16, "../openrtx/fonts/icons_16_1.bin"));
#endif
