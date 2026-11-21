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
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include "bootloader.h"
#include "monitor.h"
#include "testsignal.h"
#include "canceller.h"
#include "rtx/rtx.h"
#include "peripherals/rtc.h"
#include "optionbytes.h"
#include "core/audio_codec.h"
#include "core/audio_path.h"
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
 *   tg <shape> [hz] [amp]     test signal on DAC1 (PA4), 48 kHz samples from
 *                             TMR6: 0 off, 1 sine, 2 square; amp 0-7FF
 *                             (default 400) around mid-scale; 3 white noise
 *   cx <0|1>                  receive audio canceller correction off/on
 *   rtc                       read the RTC (UTC)
 *   rtc <yy> <mm> <dd> <hh> <mi> <ss> <wd>
 *                             set the RTC (UTC); decimal numbers, wd day of
 *                             the week 1-7, Monday first
 *   mode <fm|m17>             operating mode, set in the RTX configuration
 *                             only: the UI still shows its own, and puts it
 *                             back at its next change
 *   c2 s                      start decoding Codec2 3200 frames to the
 *                             speaker (MCU -> speaker audio path)
 *   c2 p <hex>                push up to 4 frames (8 bytes each), blocking
 *                             while the decoder queue is full
 *   c2 e                      stop decoding
 *   hp                        largest block the heap can allocate now
 *   ob                        option bytes: SRAM size and Flash protection
 *   ob 224 i-know             select 224 kB of SRAM in the option bytes, see
 *                             optionbytes.c; effective after a restart
 *   xr <v|u> <reg>            read a BK4829 register (v: VHF chip, u: UHF/HF)
 *   xw <v|u> <reg> <value>    write a BK4829 register
 *   ar <channel> <count> [div]
 *                             record ADC2 <channel> (0 = PA0, 2 = PA2) at
 *                             48 kHz / div (default 6, 8 kHz), synchronous
 *                             with tg; prints the addresses of the samples
 *                             and of the DAC values written alongside, and
 *                             the count; read them with rb
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
#define MAX_ARGS 8
#define STACK_SIZE 1536
/*
 * Cooperative: the command thread is not preempted by OpenRTX threads busy
 * with the display, and only runs when a byte arrives.
 */
#define PRIORITY K_PRIO_COOP(7)

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

static const char *cmdTestSignal(char **arg)
{
    uint32_t shape, hz = TG_DEFAULT_HZ, amp = TG_DEFAULT_AMP;

    if (!parseHex(arg[0], &shape))
        return "usage: tg <shape> [hz] [amp]";
    if ((arg[1] != NULL) && !parseHex(arg[1], &hz))
        return "bad frequency";
    if ((arg[2] != NULL) && !parseHex(arg[2], &amp))
        return "bad amplitude";
    if (testsignal_start((enum testsignalShape)shape, hz, amp) < 0)
        return "bad parameters";

    return NULL;
}

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

#define AR_DEFAULT_DIV 6

#define C2_FRAME_SIZE 8
#define C2_MAX_FRAMES 4
#define C2_PUSH_TIMEOUT_MS 1000

static pathId c2Path = -1;

static int hexNibble(char c)
{
    if ((c >= '0') && (c <= '9'))
        return c - '0';
    if ((c >= 'a') && (c <= 'f'))
        return c - 'a' + 10;
    if ((c >= 'A') && (c <= 'F'))
        return c - 'A' + 10;

    return -1;
}

static const char *c2Push(const char *hex)
{
    uint8_t frames[C2_FRAME_SIZE * C2_MAX_FRAMES];
    size_t len = (hex != NULL) ? strlen(hex) : 0;

    if ((len == 0) || ((len % (2 * C2_FRAME_SIZE)) != 0)
        || (len > (2 * sizeof(frames))))
        return "need 1 to 4 frames of 8 bytes";

    for (size_t i = 0; i < len / 2; i++) {
        int hi = hexNibble(hex[2 * i]);
        int lo = hexNibble(hex[(2 * i) + 1]);
        if ((hi < 0) || (lo < 0))
            return "bad hex";
        frames[i] = (uint8_t)((hi << 4) | lo);
    }

    /* Not blocking: a stalled decoder must not hang the monitor */
    for (size_t f = 0; f < len / (2 * C2_FRAME_SIZE); f++) {
        int64_t deadline = k_uptime_get() + C2_PUSH_TIMEOUT_MS;
        while (codec_pushFrame(&frames[f * C2_FRAME_SIZE], false) == -EAGAIN) {
            if (k_uptime_get() > deadline)
                return "decoder stalled";
            k_msleep(2);
        }
    }

    return NULL;
}

static const char *cmdCodec2(char **arg)
{
    if (arg[0] == NULL)
        return "usage: c2 <s|p|e> [hex]";

    if (strcmp(arg[0], "s") == 0) {
        if (c2Path >= 0)
            return "already running";
        c2Path = audioPath_request(SOURCE_MCU, SINK_SPK, PRIO_PROMPT);
        if (c2Path < 0)
            return "audio path busy";
        codec_init();
        if (!codec_startDecode(c2Path))
            return "decoder start failed";
        return NULL;
    }

    if (strcmp(arg[0], "p") == 0) {
        if (c2Path < 0)
            return "not running";
        return c2Push(arg[1]);
    }

    if (strcmp(arg[0], "e") == 0) {
        if (c2Path < 0)
            return "not running";
        codec_stop(c2Path);
        codec_terminate();
        audioPath_release(c2Path);
        c2Path = -1;
        return NULL;
    }

    return "usage: c2 <s|p|e> [hex]";
}

#define HP_STEP 256

static const char *cmdHeap(char **arg)
{
    (void)arg;

    size_t size = 224 * 1024;
    while (size > 0) {
        void *p = malloc(size);
        if (p != NULL) {
            free(p);
            break;
        }
        size -= HP_STEP;
    }

    out("%u\n", (unsigned int)size);

    return NULL;
}

#define OB_CONFIRM "i-know"

static const char *cmdOptionBytes(char **arg)
{
    if (arg[0] == NULL) {
        out("sram %s fap %s usd %08lX\n",
            optionbytes_sram224() ? "224k" : "96k",
            (flash_fap_status_get() != RESET) ? "protected" : "unprotected",
            (unsigned long)FLASH->usd);
        return NULL;
    }

    if ((strcmp(arg[0], "224") != 0) || (arg[1] == NULL)
        || (strcmp(arg[1], OB_CONFIRM) != 0))
        return "usage: ob [224 " OB_CONFIRM "]";

    int ret = optionbytes_setSram224();
    if (ret == -EAGAIN)
        return "battery too low";
    if (ret == -EPERM)
        return "Flash not protected, not handled";
    if (ret < 0)
        return "failed, check with ob before restarting";

    return NULL;
}

static const char *cmdCanceller(char **arg)
{
    uint32_t on;

    if (!parseHex(arg[0], &on) || (on > 1))
        return "usage: cx <0|1>";

    canceller_enable(on != 0);

    return NULL;
}

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

static const char *cmdMode(char **arg)
{
    rtxStatus_t cfg = rtx_getCurrentStatus();

    if ((arg[0] != NULL) && (strcmp(arg[0], "fm") == 0))
        cfg.opMode = OPMODE_FM;
    else if ((arg[0] != NULL) && (strcmp(arg[0], "m17") == 0))
        cfg.opMode = OPMODE_M17;
    else
        return "usage: mode <fm|m17>";

    rtx_configure(&cfg);

    return NULL;
}

static const char *cmdAudioRecord(char **arg)
{
    uint32_t channel, count, div = AR_DEFAULT_DIV;
    const uint16_t *samples, *dacSamples;

    if (!parseHex(arg[0], &channel) || !parseHex(arg[1], &count))
        return "usage: ar <channel> <count> [div]";
    if ((arg[2] != NULL) && !parseHex(arg[2], &div))
        return "bad divider";
    if (testsignal_record(channel, count, div, &samples, &dacSamples) < 0)
        return "bad parameters";

    out("%08lX %08lX %lX\n", (unsigned long)samples, (unsigned long)dacSamples,
        (unsigned long)count);

    return NULL;
}

static const struct {
    const char *name;
    const char *(*func)(char **arg);
} commands[] = {
    { .name = "ping", .func = cmdPing },
    { .name = "rb", .func = cmdReadBytes },
    { .name = "rw", .func = cmdReadWords },
    { .name = "wb", .func = cmdWriteByte },
    { .name = "ww", .func = cmdWriteWord },
    { .name = "update", .func = cmdUpdate },
    { .name = "tg", .func = cmdTestSignal },
    { .name = "ar", .func = cmdAudioRecord },
    { .name = "c2", .func = cmdCodec2 },
    { .name = "hp", .func = cmdHeap },
    { .name = "cx", .func = cmdCanceller },
    { .name = "mode", .func = cmdMode },
    { .name = "rtc", .func = cmdRtc },
    { .name = "ob", .func = cmdOptionBytes },
    { .name = "xr", .func = cmdBkRead },
    { .name = "xw", .func = cmdBkWrite },
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
    testsignal_init();

    return 0;
}

SYS_INIT(monitor_init, APPLICATION, 99);
