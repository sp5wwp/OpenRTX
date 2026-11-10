/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <interfaces/delays.h>
#include <interfaces/arch_registers.h>
#include <miosix.h>

/*
 * Busy-wait delays based on the DWT cycle counter, enabled in IRQbspInit().
 * They follow SystemCoreClock, so they stay correct when the clock changes.
 */

static void delayCycles(uint32_t cycles)
{
    uint32_t start = DWT->CYCCNT;

    while ((DWT->CYCCNT - start) < cycles)
        ;
}

void delayUs(unsigned int useconds)
{
    delayCycles(useconds * (SystemCoreClock / 1000000));
}

void delayMs(unsigned int mseconds)
{
    for (unsigned int i = 0; i < mseconds; i++)
        delayCycles(SystemCoreClock / 1000);
}

void sleepFor(unsigned int seconds, unsigned int mseconds)
{
    unsigned int time = (seconds * 1000) + mseconds;
    miosix::Thread::sleep(time);
}

void sleepUntil(long long timestamp)
{
    miosix::Thread::sleepUntil(timestamp);
}

long long getTick()
{
    return miosix::getTick();
}
