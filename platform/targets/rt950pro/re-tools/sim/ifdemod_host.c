/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* The receiver's IF demodulator, on the host: at32_adc.c as it is */
#include "../../../../drivers/audio/at32_adc.c"

void ifDemodReset(void)
{
    memset(&adc, 0, sizeof(adc));
    adc.prev = -1;
}

void ifDemod(const uint16_t *in, int16_t *out, size_t count)
{
    demodulate(in, out, count);
}
