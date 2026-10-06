/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "interfaces/delays.h"
#include <time.h>
#include <unistd.h>
#include <stdio.h>

/**
 * Implementation of the delay functions for x86_64.
 */

void delayUs(unsigned int useconds)
{
    usleep(useconds);
}

void delayMs(unsigned int mseconds)
{
    usleep(mseconds*1000);
}

void sleepFor(unsigned int seconds, unsigned int mseconds)
{
    unsigned int time = (seconds * 1000) + mseconds;
    delayMs(time);
}

void sleepUntil(long long timestamp)
{
    long long delta = timestamp - getTick();
    if(delta <= 0) return;
    delayMs(delta);
}

long long getTick()
{
    /*
     * Return current time in milliseconds, consistently with the miosix kernel
     * having a tick rate of 1kHz. Like the kernel tick, it comes from a
     * monotonic clock so that it keeps counting steadily when the wall clock
     * is changed or frozen.
     */

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long long milliseconds = ts.tv_sec*1000LL + ts.tv_nsec/1000000;
    return milliseconds;
}
