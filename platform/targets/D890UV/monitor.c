/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <drivers/baseband/WX4818N.h>
#include <drivers/NVM/spiNand_D890UV.h>
#include <drivers/USB/usb.h>
#include <interfaces/delays.h>
#include <hwconfig.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "monitor.h"
#include "bootloader.h"

/*
 * Debug monitor on the USB serial port. One command per line, numbers in
 * hexadecimal (with or without 0x). Every reply ends with a line "OK" or
 * "ERR <reason>".
 *
 *   ping                      -> D890UV
 *   rb <addr> <len>           read bytes, hex, 32 bytes per line
 *   rw <addr> [count]         read 32-bit words, one per line
 *   wb <addr> <value>         write a byte
 *   ww <addr> <value>         write a 32-bit word
 *   nr <addr> <len>           read the SPI NAND (addr = page << 12 | column)
 *   xr <a|b> <reg>            read a WX4818N register
 *   xw <a|b> <reg> <value>    write a WX4818N register
 *   xg <a|b> <pin> <0|1>      set a WX4818N GPIO
 *   update                    restart into the bootloader update mode
 *
 * Memory accesses are not checked: an unmapped address faults the MCU.
 */

#define MAX_READ 4096
#define NAND_PAGE 0x1000

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

    usb_write(buf, len);
}

static void hexLines(uint32_t addr, const uint8_t *data, size_t len)
{
    for (size_t pos = 0; pos < len; pos += 32) {
        char line[8 + 2 + 32 * 2 + 2];
        int n = snprintf(line, sizeof(line), "%08lX ",
                         (unsigned long)(addr + pos));

        for (size_t i = pos; (i < len) && (i < pos + 32); i++)
            n += snprintf(line + n, sizeof(line) - n, "%02X", data[i]);

        line[n++] = '\n';
        usb_write(line, n);
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

static const struct wx4818n *parseChip(const char *str)
{
    if (str == NULL)
        return NULL;
    if ((strcmp(str, "a") == 0) || (strcmp(str, "A") == 0))
        return &wx4818nA;
    if ((strcmp(str, "b") == 0) || (strcmp(str, "B") == 0))
        return &wx4818nB;

    return NULL;
}

static const char *cmdReadBytes(char **arg)
{
    uint32_t addr, len;
    if (!parseHex(arg[0], &addr) || !parseHex(arg[1], &len))
        return "usage: rb <addr> <len>";
    if (len > MAX_READ)
        return "length too large";

    hexLines(addr, (const uint8_t *)addr, len);
    return NULL;
}

static const char *cmdReadWords(char **arg)
{
    uint32_t addr, count = 1;
    if (!parseHex(arg[0], &addr)
        || ((arg[1] != NULL) && !parseHex(arg[1], &count)))
        return "usage: rw <addr> [count]";
    if ((addr & 3) != 0)
        return "address not aligned";
    if (count > MAX_READ / 4)
        return "count too large";

    for (uint32_t i = 0; i < count; i++) {
        uint32_t a = addr + 4 * i;
        out("%08lX %08lX\n", (unsigned long)a,
            (unsigned long)*((volatile uint32_t *)a));
    }

    return NULL;
}

static const char *cmdWriteByte(char **arg)
{
    uint32_t addr, value;
    if (!parseHex(arg[0], &addr) || !parseHex(arg[1], &value) || (value > 0xFF))
        return "usage: wb <addr> <value>";

    *((volatile uint8_t *)addr) = value;
    return NULL;
}

static const char *cmdWriteWord(char **arg)
{
    uint32_t addr, value;
    if (!parseHex(arg[0], &addr) || !parseHex(arg[1], &value))
        return "usage: ww <addr> <value>";
    if ((addr & 3) != 0)
        return "address not aligned";

    *((volatile uint32_t *)addr) = value;
    return NULL;
}

static const char *cmdNandRead(char **arg)
{
    static uint8_t buf[256];
    uint32_t addr, len;

    if (!parseHex(arg[0], &addr) || !parseHex(arg[1], &len))
        return "usage: nr <addr> <len>";
    if (len > MAX_READ)
        return "length too large";

    while (len > 0) {
        // Do not cross a page boundary in a single read
        uint32_t chunk = NAND_PAGE - (addr & (NAND_PAGE - 1));
        if (chunk > sizeof(buf))
            chunk = sizeof(buf);
        if (chunk > len)
            chunk = len;

        if (spiNand_read(addr, buf, chunk) < 0)
            return "NAND timeout";

        hexLines(addr, buf, chunk);
        addr += chunk;
        len -= chunk;
    }

    return NULL;
}

static const char *cmdWxRead(char **arg)
{
    const struct wx4818n *dev = parseChip(arg[0]);
    uint32_t reg;
    if ((dev == NULL) || !parseHex(arg[1], &reg) || (reg > 0x7F))
        return "usage: xr <a|b> <reg>";

    out("%04X\n", wx4818n_readReg(dev, reg));
    return NULL;
}

static const char *cmdWxWrite(char **arg)
{
    const struct wx4818n *dev = parseChip(arg[0]);
    uint32_t reg, value;
    if ((dev == NULL) || !parseHex(arg[1], &reg) || (reg > 0x7F)
        || !parseHex(arg[2], &value) || (value > 0xFFFF))
        return "usage: xw <a|b> <reg> <value>";

    wx4818n_writeReg(dev, reg, value);
    return NULL;
}

static const char *cmdWxGpio(char **arg)
{
    const struct wx4818n *dev = parseChip(arg[0]);
    uint32_t pin, level;
    if ((dev == NULL) || !parseHex(arg[1], &pin) || (pin > 7)
        || !parseHex(arg[2], &level) || (level > 1))
        return "usage: xg <a|b> <pin> <0|1>";

    wx4818n_setGpio(dev, pin, level);
    return NULL;
}

static const char *cmdUpdate(char **arg)
{
    (void)arg;

    if (bootloader_isKnown() == false)
        return "unknown bootloader version";

    // Let the reply reach the host before the USB device goes away
    out("OK\n");
    sleepFor(0, 100);
    bootloader_restartToUpdate();

    return NULL;
}

static const char *cmdPing(char **arg)
{
    (void)arg;
    out("D890UV\n");
    return NULL;
}

struct command {
    const char *name;
    const char *(*func)(char **arg);
};

static const struct command commands[] = {
    { "ping", cmdPing },     { "rb", cmdReadBytes }, { "rw", cmdReadWords },
    { "wb", cmdWriteByte },  { "ww", cmdWriteWord }, { "nr", cmdNandRead },
    { "xr", cmdWxRead },     { "xw", cmdWxWrite },   { "xg", cmdWxGpio },
    { "update", cmdUpdate },
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

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (strcmp(cmd, commands[i].name) != 0)
            continue;

        const char *err = commands[i].func(arg);
        if (err == NULL)
            out("OK\n");
        else
            out("ERR %s\n", err);

        return;
    }

    out("ERR unknown command\n");
}
