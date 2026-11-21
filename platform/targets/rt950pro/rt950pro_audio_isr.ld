/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Code of the 48 kHz audio interrupt path (AUDIO_ISR_CODE, dac_at32.h), right
 * after the model signatures, so that it is within the zero-wait Flash.
 */
KEEP(*(.audio_isr))
