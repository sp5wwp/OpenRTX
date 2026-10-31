/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <soc.h>
#include <stddef.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "peripherals/rtc.h"

LOG_MODULE_REGISTER(rtc_at32, LOG_LEVEL_INF);

#define RTC_CLOCK CRM_RTC_CLOCK_HEXT_DIV
#define RTC_DIVIDER \
    (DT_PROP(DT_NODELABEL(clk_hext), clock_frequency) / 128U - 1U)
_Static_assert(DT_PROP(DT_NODELABEL(clk_hext), clock_frequency) % 128U == 0,
               "RTC requires an integral HEXT/128 frequency");
#define RTC_WAIT_MS 1000
/* Reserve BPR DT42 for the epoch/version marker and DST state. */
#define RTC_FORMAT 0x5254U
#define RTC_DST 1U
#define RTC_CENTURY_SECONDS 3155760000U
#define SECONDS_PER_DAY 86400U

K_MUTEX_DEFINE(rtc_mutex);

static _Noreturn void rtc_panic(void)
{
    k_panic();
    /* Custom fatal handlers can return; never publish invalid RTC values. */
    abort();
}

static unsigned int days_in_month(unsigned int month, unsigned int year)
{
    static const uint8_t days[] = { 31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31 };
    return days[month - 1] + (month == 2 && year % 4 == 0);
}

static bool calendar_from_seconds(uint32_t seconds, datetime_t *time)
{
    if (seconds >= RTC_CENTURY_SECONDS)
        return false;
    datetime_t result = { 0 };
    uint32_t days = seconds / SECONDS_PER_DAY;
    result.day = (days + 5) % 7 + 1; /* 2000-01-01 was Saturday. */
    result.hour = seconds % SECONDS_PER_DAY / 3600;
    result.minute = seconds % 3600 / 60;
    result.second = seconds % 60;

    unsigned int year = 0;
    while (days >= (year % 4 == 0 ? 366U : 365U)) {
        days -= year % 4 == 0 ? 366U : 365U;
        year++;
    }
    result.year = year;
    unsigned int month = 1;
    while (days >= days_in_month(month, year)) {
        days -= days_in_month(month, year);
        month++;
    }
    result.month = month;
    result.date = days + 1;
    *time = result;
    return true;
}

static bool calendar_to_seconds(datetime_t time, uint32_t *seconds)
{
    if (time.year > 99 || time.month < 1 || time.month > 12 || time.date < 1
        || (unsigned int)time.date > days_in_month(time.month, time.year)
        || time.hour < 0 || time.hour > 23 || time.minute < 0
        || time.minute > 59 || time.second < 0 || time.second > 59)
        return false;
    uint32_t days = 0;
    for (unsigned int year = 0; year < time.year; year++)
        days += year % 4 == 0 ? 366 : 365;
    for (unsigned int month = 1; month < (unsigned int)time.month; month++)
        days += days_in_month(month, time.year);
    days += time.date - 1;
    *seconds = days * SECONDS_PER_DAY + time.hour * 3600U + time.minute * 60U
             + time.second;
    return true;
}

static volatile uint32_t *backup_register(unsigned int index)
{
    size_t offset = index < 10 ? offsetof(bpr_type, dt1) + index * 4 :
                                 offsetof(bpr_type, dt11) + (index - 10) * 4;
    return (volatile uint32_t *)((uintptr_t)BPR + offset);
}

static void select_clock(void)
{
    if (CRM->bpdc_bit.rtcsel != CRM_RTC_CLOCK_NOCLK
        && CRM->bpdc_bit.rtcsel != RTC_CLOCK) {
        /* RTCSEL is locked until reset; preserve unrelated backup data. */
        uint16_t saved[42];
        for (unsigned int i = 0; i < 42; i++)
            saved[i] = *backup_register(i);
        crm_battery_powered_domain_reset(TRUE);
        __DSB();
        crm_battery_powered_domain_reset(FALSE);
        for (unsigned int i = 0; i < 42; i++)
            *backup_register(i) = saved[i];
        LOG_WRN("RTC clock changed to HEXT/128; calendar reinitialized");
    }
    crm_rtc_clock_select(RTC_CLOCK);
    crm_rtc_clock_enable(TRUE);
    if (CRM->bpdc_bit.rtcsel != RTC_CLOCK) {
        LOG_ERR("RTC clock source could not be selected");
        rtc_panic();
    }
}

static void wait_flag(volatile uint32_t *reg, uint32_t mask)
{
    int64_t deadline = k_uptime_get() + RTC_WAIT_MS;
    while ((*reg & mask) == 0) {
        if (k_uptime_get() >= deadline) {
            LOG_ERR("RTC register synchronization timed out (0x%x)", mask);
            rtc_panic();
        }
        k_msleep(1);
    }
}

static uint32_t read_counter(void)
{
    for (unsigned int attempt = 0; attempt < 8; attempt++) {
        uint32_t high = RTC->cnth & 0xffffU;
        uint32_t low = RTC->cntl & 0xffffU;
        if (high == (RTC->cnth & 0xffffU))
            return (high << 16) | low;
    }
    LOG_ERR("RTC counter could not be read coherently");
    rtc_panic();
}

static void write_counter(uint32_t seconds)
{
    wait_flag(&RTC->ctrll, RTC_CFGF_FLAG);
    RTC->ctrll_bit.cfgen = 1;
    RTC->cnth = seconds >> 16;
    RTC->cntl = seconds & 0xffffU;
    RTC->ctrll_bit.cfgen = 0;
    wait_flag(&RTC->ctrll, RTC_CFGF_FLAG);
}

static datetime_t read_time(void)
{
    datetime_t time;
    if (!calendar_from_seconds(read_counter(), &time)) {
        LOG_ERR("RTC counter outside supported years 2000-2099");
        rtc_panic();
    }
    return time;
}

static bool write_time(datetime_t time)
{
    uint32_t seconds;
    if (!calendar_to_seconds(time, &seconds)) {
        LOG_ERR("Invalid RTC date/time");
        return false;
    }
    write_counter(seconds);
    return true;
}

void rtc_init(void)
{
    k_mutex_lock(&rtc_mutex, K_FOREVER);
    crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_BPR_PERIPH_CLOCK, TRUE);
    __DSB();
    PWC->ctrl_bit.bpwen = 1;

    bool retained = (BPR->dt42 & ~RTC_DST) == RTC_FORMAT
                 && CRM->bpdc_bit.rtcsel == RTC_CLOCK && CRM->bpdc_bit.rtcen;
    crm_clock_source_enable(CRM_CLOCK_SOURCE_HEXT, TRUE);
    wait_flag(&CRM->ctrl, BIT(17));
    select_clock();

    RTC->ctrll_bit.updf = 0;
    wait_flag(&RTC->ctrll, RTC_UPDF_FLAG);
    wait_flag(&RTC->ctrll, RTC_CFGF_FLAG);
    RTC->ctrlh = 0;

    uint32_t divider = ((RTC->divh & 0xfU) << 16) | (RTC->divl & 0xffffU);
    datetime_t time;
    retained = retained && divider == RTC_DIVIDER
            && calendar_from_seconds(read_counter(), &time);
    if (!retained) {
        RTC->ctrll_bit.cfgen = 1;
        RTC->divh = RTC_DIVIDER >> 16;
        RTC->divl = RTC_DIVIDER & 0xffffU;
        RTC->ctrll_bit.cfgen = 0;
        wait_flag(&RTC->ctrll, RTC_CFGF_FLAG);
        write_counter(0);
        BPR->dt42 = RTC_FORMAT;
        LOG_WRN("RTC initialized to 2000-01-01 UTC; set date/time or use GPS");
    }
    LOG_INF("RTC clock: HEXT/128, %u Hz; stops when HEXT is off",
            (unsigned int)(RTC_DIVIDER + 1U));
    k_mutex_unlock(&rtc_mutex);
}

void rtc_terminate(void)
{
    /* Preserve backup state; counting requires the main crystal running. */
}

void rtc_setTime(datetime_t time)
{
    k_mutex_lock(&rtc_mutex, K_FOREVER);
    if (write_time(time))
        BPR->dt42 = RTC_FORMAT;
    k_mutex_unlock(&rtc_mutex);
}

void rtc_setHour(uint8_t hours, uint8_t minutes, uint8_t seconds)
{
    if (hours > 23 || minutes > 59 || seconds > 59) {
        LOG_ERR("Invalid RTC hour/minute/second");
        return;
    }
    k_mutex_lock(&rtc_mutex, K_FOREVER);
    datetime_t time = read_time();
    time.hour = hours;
    time.minute = minutes;
    time.second = seconds;
    if (write_time(time))
        BPR->dt42 = RTC_FORMAT;
    k_mutex_unlock(&rtc_mutex);
}

void rtc_setDate(uint8_t date, uint8_t month, uint8_t year)
{
    if (date < 1 || date > 31 || month < 1 || month > 12 || year > 99) {
        LOG_ERR("Invalid RTC date/month/year");
        return;
    }
    k_mutex_lock(&rtc_mutex, K_FOREVER);
    datetime_t time = read_time();
    time.date = date;
    time.month = month;
    time.year = year;
    write_time(time);
    k_mutex_unlock(&rtc_mutex);
}

datetime_t rtc_getTime(void)
{
    k_mutex_lock(&rtc_mutex, K_FOREVER);
    datetime_t time = read_time();
    k_mutex_unlock(&rtc_mutex);
    return time;
}

static void set_dst(bool enabled)
{
    k_mutex_lock(&rtc_mutex, K_FOREVER);
    if (((BPR->dt42 & RTC_DST) != 0) != enabled) {
        uint32_t seconds = read_counter();
        datetime_t time;
        if (!calendar_from_seconds(seconds, &time)
            || (!enabled && seconds < 3600U)
            || !calendar_from_seconds(
                enabled ? seconds + 3600U : seconds - 3600U, &time)) {
            LOG_ERR("DST change outside supported RTC calendar");
        } else {
            write_counter(enabled ? seconds + 3600U : seconds - 3600U);
            BPR->dt42 = RTC_FORMAT | (enabled ? RTC_DST : 0);
        }
    }
    k_mutex_unlock(&rtc_mutex);
}

void rtc_dstSet(void)
{
    set_dst(true);
}

void rtc_dstClear(void)
{
    set_dst(false);
}
