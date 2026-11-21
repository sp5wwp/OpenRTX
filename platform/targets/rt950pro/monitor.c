/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include "bootloader.h"
#include "monitor.h"

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
static const uint8_t blProbe[] = { 0xAA, 0x42, 0x00, 0x00, 0x00,
                                   0x00, 0x55, 0xEB, 0x55, };

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
static bool tailMatches(const uint8_t *tail, size_t tailLen,
                        const uint8_t *str, size_t len)
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

    if ((armedAt >= 0) && tailMatches(tail, tailLen, (const uint8_t *)hsUpdate,
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

static const struct {
    const char *name;
    const char *(*func)(char **arg);
} commands[] = {
    { "ping", cmdPing },       { "rb", cmdReadBytes }, { "rw", cmdReadWords },
    { "wb", cmdWriteByte },    { "ww", cmdWriteWord }, { "update", cmdUpdate },
};

void monitor_exec(char *line)
{
    char *arg[4] = { NULL };
    char *save;

    char *cmd = strtok_r(line, " \t", &save);
    if (cmd == NULL)
        return;

    for (int i = 0; i < 4; i++)
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
