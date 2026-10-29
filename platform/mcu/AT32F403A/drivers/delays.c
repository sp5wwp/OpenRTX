/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <interfaces/delays.h>
#include <zephyr/kernel.h>
#include <stdint.h>

void delayUs(unsigned int useconds)
{
    /*
     * k_usleep() rounds up to at least one system tick, use a busy wait for
     * delays shorter than one millisecond.
     */
    if (useconds < 1000)
        k_busy_wait(useconds);
    else
        k_usleep(useconds);
}

void delayMs(unsigned int mseconds)
{
    k_msleep(mseconds);
}

void sleepFor(unsigned int seconds, unsigned int mseconds)
{
    uint64_t duration = ((uint64_t)seconds * 1000) + mseconds;
    k_sleep(K_MSEC(duration));
}

void sleepUntil(long long timestamp)
{
    if (timestamp <= k_uptime_get())
        return;

    k_sleep(K_TIMEOUT_ABS_MS(timestamp));
}

long long getTick()
{
    return k_uptime_get();
}
