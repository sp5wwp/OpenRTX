/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <soc.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/ring_buffer.h>
#include "bench.h"
#include "bootloader.h"
#include "monitor.h"
#include "rtx/rtx.h"
#include "peripherals/rtc.h"
#include "core/state.h"
#include <pthread.h>
#include <drivers/baseband/BK4829.h>
#include <hwconfig.h>

/*
 * Debug monitor on the programming cable (UART4, 115200 8N1), shared with the
 * Zephyr console: log lines start with '[' and are not part of the replies.
 * One command per line, numbers in hexadecimal (with or without 0x). Every
 * reply ends with a line "OK" or "ERR <reason>".
 *
 *   ping                      -> RT950PRO
 *   rb <addr> <len>           read bytes, hex, 32 bytes per line
 *   rw <addr> [count]         read 32-bit words, one per line
 *   wb <addr> <value>         write a byte
 *   ww <addr> <value>         write a 32-bit word
 *   update                    restart into the bootloader update mode
 *   sf id | r <addr> <len> | c <addr> <len>
 *                             external SPI NOR: JEDEC ID, read, CRC-32
 *   rtc                       read the RTC (UTC)
 *   rtc <yy> <mm> <dd> <hh> <mi> <ss> <wd>
 *                             set the RTC (UTC); decimal numbers, wd day of
 *                             the week 1-7, Monday first
 *   tz <half-hours>           timezone setting (UTC offset in half hours,
 *                             decimal, may be negative), as the settings menu
 *   freq <hz>                 receive and transmit frequency, decimal, set in
 *                             the RTX configuration only, as mode
 *   mode <fm|m17>             operating mode, set in the RTX configuration
 *                             only: the UI still shows its own, and puts it
 *                             back at its next change
 *   hp                        largest block the heap can allocate now
 *   xr <v|u> <reg>            read a BK4829 register (v: VHF chip, u: UHF/HF)
 *   xw <v|u> <reg> <value>    write a BK4829 register
 *   rx                        receiver state and last M17 link setup:
 *                             source, destination, meta text; free heap;
 *                             M17 stream frames decoded, accepted, and
 *                             their corrected bit errors since start-up
 *   tt <0|1|2>                M17 tone transmitter test levels in place of
 *                             the baseband: 0 off, 1 zero level, 2 sines,
 *                             3 each gain code against the zero code,
 *                             4 a 1Hz square wave
 *   ptt <ms> | ptt off        transmit through the normal path, released
 *                             after at most 10 s (decimal ms)
 *
 * A restart into the bootloader update mode is also triggered, from the
 * UART interrupt, by:
 * - the stock firmware update handshake, so that the vendor update tool and
 *   scripts/rt950_flash.py work without keys: "PROGRAMBT9000U" (optionally
 *   followed by "-FWUP") is answered with 0x06, then "UPDATE" with 0x06;
 * - the bootloader "enter update" frame itself, which the host keeps sending
 *   until the bootloader answers it after the restart.
 *
 * The monitor is started by SYS_INIT, independently of platform_init(), so
 * that a firmware stuck during its start-up can still be updated.
 *
 * Memory accesses are not checked: an unmapped address faults the MCU.
 */

#define MON_UART DEVICE_DT_GET(DT_CHOSEN(zephyr_console))

#define RX_BUF_SIZE 256
#define LINE_SIZE 96
#define MAX_READ 4096
#define SF_CHUNK 256
#define MAX_ARGS 8
#define STACK_SIZE 1536
/*
 * Lowest application priority: long replies (memory dumps) must not delay
 * the radio threads, which refill the audio streams in real time (a
 * cooperative monitor made M17 transmissions splice stale samples).
 */
#define PRIORITY K_PRIO_PREEMPT(CONFIG_NUM_PREEMPT_PRIORITIES - 1)

/* After "PROGRAMBT9000U", "UPDATE" is accepted for this time */
#define HANDSHAKE_TIMEOUT_MS 3000

/* Time for a reply to leave the UART before the restart */
#define RESTART_DELAY_US 2000

#define ACK 0x06

static const char hsProgram[] = "PROGRAMBT9000U";
static const char hsUpdate[] = "UPDATE";

/* Bootloader "enter update" frame, see bootloader.c */
static const uint8_t blProbe[] = {
    0xAA, 0x42, 0x00, 0x00, 0x00, 0x00, 0x55, 0xEB, 0x55,
};

static const struct device *const uart = MON_UART;
static uint8_t rxData[RX_BUF_SIZE];
static struct ring_buf rxBuf;
static K_SEM_DEFINE(rxSem, 0, 1);
static K_MUTEX_DEFINE(txMutex);

/* Let the last reply leave the UART, then restart into the bootloader */
static void restartToUpdate()
{
    k_busy_wait(RESTART_DELAY_US);
    bootloader_restartToUpdate();
}

/*
 * Update triggers, matched at the end of the received byte stream whatever
 * comes around them. This runs in the UART interrupt, so that an update can
 * be started even when no thread gets to run.
 */
static bool tailMatches(const uint8_t *tail, size_t tailLen, const uint8_t *str,
                        size_t len)
{
    return (tailLen >= len) && (memcmp(tail + tailLen - len, str, len) == 0);
}

static void matchTriggers(uint8_t c)
{
    static uint8_t tail[sizeof(hsProgram) - 1];
    static size_t tailLen;
    static int64_t armedAt = -1;

    if (tailLen == sizeof(tail)) {
        memmove(tail, tail + 1, sizeof(tail) - 1);
        tailLen--;
    }
    tail[tailLen++] = c;

    if (tailMatches(tail, tailLen, blProbe, sizeof(blProbe))) {
        restartToUpdate();
    }

    if (tailMatches(tail, tailLen, (const uint8_t *)hsProgram,
                    sizeof(hsProgram) - 1)) {
        armedAt = k_uptime_get();
        uart_poll_out(uart, ACK);
        return;
    }

    if ((armedAt >= 0)
        && tailMatches(tail, tailLen, (const uint8_t *)hsUpdate,
                       sizeof(hsUpdate) - 1)) {
        if ((k_uptime_get() - armedAt) <= HANDSHAKE_TIMEOUT_MS) {
            uart_poll_out(uart, ACK);
            restartToUpdate();
        }
        armedAt = -1;
    }
}

static void uartRxCallback(const struct device *dev, void *userData)
{
    uint8_t chunk[16];
    (void)userData;

    if (!uart_irq_update(dev))
        return;

    while (uart_irq_rx_ready(dev)) {
        int len = uart_fifo_read(dev, chunk, sizeof(chunk));
        if (len <= 0)
            break;

        for (int i = 0; i < len; i++)
            matchTriggers(chunk[i]);

        ring_buf_put(&rxBuf, chunk, len);
        k_sem_give(&rxSem);
    }
}

static void monWrite(const void *data, size_t len)
{
    const uint8_t *p = data;

    for (size_t i = 0; i < len; i++)
        uart_poll_out(uart, p[i]);
}

static void out(const char *fmt, ...)
{
    char buf[160];
    va_list ap;

    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (len < 0)
        return;
    if (len >= (int)sizeof(buf))
        len = sizeof(buf) - 1;

    monWrite(buf, len);
}

static void hexLines(uint32_t addr, const uint8_t *data, size_t len)
{
    for (size_t pos = 0; pos < len; pos += 32) {
        char line[8 + 1 + 32 * 2 + 2];
        int n = snprintf(line, sizeof(line), "%08lX ",
                         (unsigned long)(addr + pos));

        for (size_t i = pos; (i < len) && (i < pos + 32); i++)
            n += snprintf(line + n, sizeof(line) - n, "%02X", data[i]);

        line[n++] = '\n';
        monWrite(line, n);

        /* The watchdog feeder runs at the same priority */
        k_yield();
    }
}

static bool parseHex(const char *str, uint32_t *value)
{
    if (str == NULL)
        return false;

    char *end;
    *value = strtoul(str, &end, 16);

    return (*end == '\0') && (end != str);
}

static const char *cmdPing(char **arg)
{
    (void)arg;
    out("RT950PRO\n");

    return NULL;
}

static const char *cmdReadBytes(char **arg)
{
    uint32_t addr, len;

    if (!parseHex(arg[0], &addr) || !parseHex(arg[1], &len))
        return "usage: rb <addr> <len>";
    if (len > MAX_READ)
        return "too long";

    hexLines(addr, (const uint8_t *)addr, len);

    return NULL;
}

/*
 * External SPI NOR flash, read only: the vendor firmware keeps its factory
 * data there (calibration). Nothing here writes or erases it.
 */
static const struct device *const extFlash =
    DEVICE_DT_GET(DT_NODELABEL(extflash));

static const char *cmdSpiFlash(char **arg)
{
    if (!device_is_ready(extFlash))
        return "flash not ready";

    if ((arg[0] != NULL) && (strcmp(arg[0], "id") == 0)) {
        uint8_t id[3];
        if (flash_read_jedec_id(extFlash, id) != 0)
            return "id read failed";
        out("%02X%02X%02X\n", id[0], id[1], id[2]);
        return NULL;
    }

    /* Small: with newlib, static RAM is taken from the heap */
    static uint8_t buf[SF_CHUNK];
    uint32_t addr, len;
    if ((arg[0] == NULL) || !parseHex(arg[1], &addr) || !parseHex(arg[2], &len))
        return "usage: sf id | sf r <addr> <len> | sf c <addr> <len>";

    /* CRC-32 (IEEE) of a region, to check a dump made with "sf r" */
    if (strcmp(arg[0], "c") == 0) {
        uint32_t crc = 0;
        for (uint32_t pos = 0; pos < len; pos += SF_CHUNK) {
            uint32_t n = MIN(SF_CHUNK, len - pos);
            if (flash_read(extFlash, addr + pos, buf, n) != 0)
                return "read failed";
            crc = crc32_ieee_update(crc, buf, n);
        }
        out("%08lX\n", (unsigned long)crc);
        return NULL;
    }

    if (strcmp(arg[0], "r") != 0)
        return "usage: sf id | sf r <addr> <len> | sf c <addr> <len>";
    if (len > MAX_READ)
        return "too long";

    for (uint32_t pos = 0; pos < len; pos += SF_CHUNK) {
        uint32_t n = MIN(SF_CHUNK, len - pos);
        if (flash_read(extFlash, addr + pos, buf, n) != 0)
            return "read failed";
        hexLines(addr + pos, buf, n);
    }

    return NULL;
}

static const char *cmdReadWords(char **arg)
{
    uint32_t addr, count = 1;

    if (!parseHex(arg[0], &addr))
        return "usage: rw <addr> [count]";
    if ((arg[1] != NULL) && !parseHex(arg[1], &count))
        return "bad count";
    if ((addr & 3) != 0)
        return "unaligned";
    if (count > MAX_READ / 4)
        return "too long";

    for (uint32_t i = 0; i < count; i++) {
        uint32_t a = addr + 4 * i;
        out("%08lX %08lX\n", (unsigned long)a,
            (unsigned long)*(volatile uint32_t *)a);
    }

    return NULL;
}

static const char *cmdWriteByte(char **arg)
{
    uint32_t addr, value;

    if (!parseHex(arg[0], &addr) || !parseHex(arg[1], &value))
        return "usage: wb <addr> <value>";

    *(volatile uint8_t *)addr = value;

    return NULL;
}

static const char *cmdWriteWord(char **arg)
{
    uint32_t addr, value;

    if (!parseHex(arg[0], &addr) || !parseHex(arg[1], &value))
        return "usage: ww <addr> <value>";
    if ((addr & 3) != 0)
        return "unaligned";

    *(volatile uint32_t *)addr = value;

    return NULL;
}

static const char *cmdUpdate(char **arg)
{
    (void)arg;

    out("OK\n");
    restartToUpdate();

    return NULL;
}

#define TG_DEFAULT_HZ 1000
#define TG_DEFAULT_AMP 0x400

static const struct BK4829 *parseChip(const char *str)
{
    if (str == NULL)
        return NULL;
    if (strcmp(str, "v") == 0)
        return &rt950_bk4829_vhf;
    if (strcmp(str, "u") == 0)
        return &rt950_bk4829_uhf_hf;

    return NULL;
}

static const char *cmdBkRead(char **arg)
{
    const struct BK4829 *chip = parseChip(arg[0]);
    uint32_t reg;

    if ((chip == NULL) || !parseHex(arg[1], &reg) || (reg > 0x7F))
        return "usage: xr <v|u> <reg>";

    out("%04X\n", bk4829_read_reg(chip, (bk4829_reg_t)reg));

    return NULL;
}

static const char *cmdBkWrite(char **arg)
{
    const struct BK4829 *chip = parseChip(arg[0]);
    uint32_t reg, value;

    if ((chip == NULL) || !parseHex(arg[1], &reg) || (reg > 0x7F)
        || !parseHex(arg[2], &value) || (value > 0xFFFF))
        return "usage: xw <v|u> <reg> <value>";

    bk4829_write_reg(chip, (bk4829_reg_t)reg, value);

    return NULL;
}

/*
 * Bench transmitter control. The bench PTT releases itself after its time,
 * at most PTT_MAX_MS: a lost serial link cannot leave the radio transmitting.
 */
#define PTT_MAX_MS 10000

static volatile bool benchPtt;

static void pttExpired(struct k_timer *timer)
{
    (void)timer;
    benchPtt = false;
}

static K_TIMER_DEFINE(pttTimer, pttExpired, NULL);

bool bench_getPtt(void)
{
    return benchPtt;
}

static const char *cmdPtt(char **arg)
{
    if (arg[0] == NULL)
        return "usage: ptt <ms> | ptt off";

    if (strcmp(arg[0], "off") == 0) {
        benchPtt = false;
        k_timer_stop(&pttTimer);
        return NULL;
    }

    char *end;
    unsigned long ms = strtoul(arg[0], &end, 10);
    if ((*end != '\0') || (ms == 0) || (ms > PTT_MAX_MS))
        return "usage: ptt <ms, 1 to 10000> | ptt off";

    k_timer_start(&pttTimer, K_MSEC(ms), K_NO_WAIT);
    benchPtt = true;

    return NULL;
}

#define HP_STEP 256

static size_t largestFreeBlock(void)
{
    size_t size = 224 * 1024;
    while (size > 0) {
        void *p = malloc(size);
        if (p != NULL) {
            free(p);
            break;
        }
        size -= HP_STEP;
    }

    return size;
}

static const char *cmdHeap(char **arg)
{
    (void)arg;

    out("%u\n", (unsigned int)largestFreeBlock());

    return NULL;
}

#define OB_CONFIRM "i-know"

static bool parseDec(const char *str, uint32_t max, int8_t *value)
{
    if (str == NULL)
        return false;

    char *end;
    unsigned long v = strtoul(str, &end, 10);
    if ((*end != '\0') || (end == str) || (v > max))
        return false;

    *value = (int8_t)v;

    return true;
}

static const char *cmdRtc(char **arg)
{
    if (arg[0] == NULL) {
        datetime_t t = rtc_getTime();
        out("20%02u-%02u-%02u %02u:%02u:%02u UTC, day %u\n", t.year, t.month,
            t.date, t.hour, t.minute, t.second, t.day);
        return NULL;
    }

    datetime_t t;
    int8_t year;
    if (!parseDec(arg[0], 99, &year) || !parseDec(arg[1], 12, &t.month)
        || !parseDec(arg[2], 31, &t.date) || !parseDec(arg[3], 23, &t.hour)
        || !parseDec(arg[4], 59, &t.minute) || !parseDec(arg[5], 59, &t.second)
        || !parseDec(arg[6], 7, &t.day))
        return "usage: rtc [yy mm dd hh mi ss wd]";

    t.year = (uint8_t)year;
    rtc_setTime(t);

    return NULL;
}

/* Largest UTC offset, in half hours: UTC+14 */
#define TZ_MAX_HALF_HOURS 28

static const char *cmdTimezone(char **arg)
{
    if (arg[0] == NULL)
        return "usage: tz <half-hours>";

    char *end;
    long tz = strtol(arg[0], &end, 10);
    if ((*end != '\0') || (end == arg[0]) || (tz > TZ_MAX_HALF_HOURS)
        || (tz < -TZ_MAX_HALF_HOURS))
        return "usage: tz <half-hours>";

    pthread_mutex_lock(&state_mutex);
    state.settings.utc_timezone = (int8_t)tz;
    pthread_mutex_unlock(&state_mutex);

    return NULL;
}

/*
 * Mode and frequency go to the radio state, as the UI sets them, then to the
 * rtx thread: a configuration given to the rtx thread alone is overwritten by
 * the next UI synchronisation, and two commands in a row would read the
 * configuration before the first one is applied.
 */
static void configureRtx(void)
{
    rtxStatus_t cfg = *rtx_getStatus();

    pthread_mutex_lock(&state_mutex);
    cfg.opMode = state.channel.mode;
    cfg.rxFrequency = state.channel.rx_frequency;
    cfg.txFrequency = state.channel.tx_frequency;
    pthread_mutex_unlock(&state_mutex);

    rtx_configure(&cfg);
}

static const char *cmdFreq(char **arg)
{
    if (arg[0] == NULL)
        return "usage: freq <hz>";

    char *end;
    unsigned long hz = strtoul(arg[0], &end, 10);
    if ((*end != '\0') || (end == arg[0]))
        return "usage: freq <hz>";

    pthread_mutex_lock(&state_mutex);
    state.channel.rx_frequency = hz;
    state.channel.tx_frequency = hz;
    pthread_mutex_unlock(&state_mutex);
    configureRtx();

    return NULL;
}

static const char *cmdMode(char **arg)
{
    uint8_t mode;

    if ((arg[0] != NULL) && (strcmp(arg[0], "fm") == 0))
        mode = OPMODE_FM;
    else if ((arg[0] != NULL) && (strcmp(arg[0], "m17") == 0))
        mode = OPMODE_M17;
    else
        return "usage: mode <fm|m17>";

    pthread_mutex_lock(&state_mutex);
    state.channel.mode = mode;
    pthread_mutex_unlock(&state_mutex);
    configureRtx();

    return NULL;
}

/* Bench: tone transmitter test levels (drivers/baseband/BK4829_tone.h) */
extern volatile uint8_t bk4829ToneTest;

static const char *cmdToneTest(char **arg)
{
    uint32_t mode;

    if ((arg[0] == NULL) || !parseHex(arg[0], &mode) || (mode > 4))
        return "usage: tt <0 baseband | 1 zero | 2 sines | 3 codes | 4 1Hz>";

    bk4829ToneTest = (uint8_t)mode;

    return NULL;
}

/* M17 stream frame counters (protocols/M17/FrameDecoder.hpp) */
extern struct {
    uint32_t frames;
    uint32_t accepted;
    uint32_t bitErrors;
} m17StreamStats;

/* Receiver state: operating mode and status, and the last M17 link setup */
static const char *cmdRxStatus(char **arg)
{
    (void)arg;

    const rtxStatus_t *s = rtx_getStatus();
    out("mode %u status %u lsf %u\n", (unsigned)s->opMode,
        (unsigned)s->opStatus, (unsigned)s->lsfOk);
    out("src %.10s dst %.10s\n", s->M17_src, s->M17_dst);
    out("meta %.52s\n", s->M17_meta_text);
    out("heap %u\n", (unsigned)largestFreeBlock());
    out("frames %lu accepted %lu biterrors %lu\n",
        (unsigned long)m17StreamStats.frames,
        (unsigned long)m17StreamStats.accepted,
        (unsigned long)m17StreamStats.bitErrors);

    return NULL;
}

static const struct {
    const char *name;
    const char *(*func)(char **arg);
} commands[] = {
    { .name = "ping", .func = cmdPing },
    { .name = "rb", .func = cmdReadBytes },
    { .name = "sf", .func = cmdSpiFlash },
    { .name = "rw", .func = cmdReadWords },
    { .name = "wb", .func = cmdWriteByte },
    { .name = "ww", .func = cmdWriteWord },
    { .name = "update", .func = cmdUpdate },
    { .name = "hp", .func = cmdHeap },
    { .name = "ptt", .func = cmdPtt },
    { .name = "mode", .func = cmdMode },
    { .name = "freq", .func = cmdFreq },
    { .name = "tz", .func = cmdTimezone },
    { .name = "rtc", .func = cmdRtc },
    { .name = "xr", .func = cmdBkRead },
    { .name = "xw", .func = cmdBkWrite },
    { .name = "rx", .func = cmdRxStatus },
    { .name = "tt", .func = cmdToneTest },
};

void monitor_exec(char *line)
{
    char *arg[MAX_ARGS] = { NULL };
    char *save;

    char *cmd = strtok_r(line, " \t", &save);
    if (cmd == NULL)
        return;

    for (int i = 0; i < MAX_ARGS; i++)
        arg[i] = strtok_r(NULL, " \t", &save);

    k_mutex_lock(&txMutex, K_FOREVER);

    const char *err = "unknown command";
    for (size_t i = 0; i < ARRAY_SIZE(commands); i++) {
        if (strcmp(cmd, commands[i].name) == 0) {
            err = commands[i].func(arg);
            break;
        }
    }

    if (err == NULL)
        out("OK\n");
    else
        out("ERR %s\n", err);

    k_mutex_unlock(&txMutex);
}

static void monitorTask(void *p1, void *p2, void *p3)
{
    static char line[LINE_SIZE];
    size_t lineLen = 0;
    bool overflow = false;
    (void)p1;
    (void)p2;
    (void)p3;

    while (true) {
        uint8_t c;

        if (ring_buf_get(&rxBuf, &c, 1) == 0) {
            k_sem_take(&rxSem, K_FOREVER);
            continue;
        }

        if ((c == '\r') || (c == '\n')) {
            line[lineLen] = '\0';
            if (!overflow && (lineLen > 0))
                monitor_exec(line);
            lineLen = 0;
            overflow = false;
        } else if (lineLen < sizeof(line) - 1) {
            line[lineLen++] = c;
        } else {
            overflow = true;
        }
    }
}

/*
 * Started by the kernel after the SYS_INIT functions, so the receive buffer
 * is set up by then. k_thread_start() cannot be used from SYS_INIT: static
 * threads are created only after the APPLICATION level.
 */
K_THREAD_DEFINE(monitorThread, STACK_SIZE, monitorTask, NULL, NULL, NULL,
                PRIORITY, 0, 0);

static int monitor_init(void)
{
    if (!device_is_ready(uart))
        return -ENODEV;

    ring_buf_init(&rxBuf, sizeof(rxData), rxData);

    int ret = uart_irq_callback_user_data_set(uart, uartRxCallback, NULL);
    if (ret < 0)
        return ret;

    uart_irq_rx_enable(uart);

    return 0;
}

SYS_INIT(monitor_init, APPLICATION, 99);
