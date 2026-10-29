/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Model signatures checked by the RT-950 Pro vendor bootloader, placed right
 * after the vector table at the start of the image (see board_init.c):
 * - offset 0x3E0, compared during the firmware upload handshake;
 * - offset 0x7E0, compared at boot before jumping to the application. When it
 *   does not match, the bootloader erases the firmware.
 * The linker fails if the content placed before these offsets grows over
 * them, since the location counter cannot move backwards.
 */
. = 0x3E0;
KEEP(*(.rt950_model_sig))
. = 0x7E0;
KEEP(*(.rt950_boot_model_sig))
