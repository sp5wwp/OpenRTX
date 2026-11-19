/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Remote control over the programming cable, for automated tests (C62_REMOTE
 * builds). The cable's host TX line is on GPIOA_15, which is both UART2 RXD
 * and the PTT button: this switches the pad to UART2 RXD, so PTT is not
 * available in these builds.
 *
 * Commands are text lines (LF or CR terminated). Replies are control
 * packets of the baseband dump (baseband_dump_c62.h), so they share the TX
 * line with the sample packets. Every command gets a reply starting with
 * its own letter; errors start with "E".
 *
 *   ping                 "pong"
 *   s                    status line (see status())
 *   a <ms>               send the status every <ms> (0: off)
 *   f <hz>               set the RX/TX frequency
 *   m fm|m17|t           set the operating mode
 *   k <key> [l]          press a key (l: long press): 0-9 star hash enter
 *                        esc up down left right moni f1
 *   p                    list the TETRA demodulator parameters
 *   p <name> <value>     set one: costAlpha timingKp timingKi fitAlpha
 *                        eqMu softScale eqBiasMu fitHold acqEnergy
 *                        okHold ctxMu ctxSpan
 *   r [0|1]              restart the TETRA receiver; 1 (default) starts
 *                        the equalizer from the measured taps, 0 not
 *   d 0|1                stop or resume recording baseband samples
 *   v <0-15>             set the volume (OpenRTX 0-255 scale, 17 per step)
 *   fo [hz]              read or set the fine RX tuning offset
 *   rr <reg>             read a BK4819 register (hex or decimal)
 *   rw <reg> <value>     write a BK4819 register; the radio driver may
 *                        overwrite it at the next mode or frequency change
 *   afc [0|1]            read or set the TETRA frequency correction (a
 *                        setting, saved like one); also shows the stored
 *                        correction
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <IOMuxManager.h>
#include "core/event.h"
#include "core/input.h"
#include "core/state.h"
#include "core/ui.h"
#include "interfaces/keyboard.h"
#include "rtx/rtx.h"
#include <hwconfig.h>
#include "drivers/baseband/BK4819.h"
#include "baseband_dump_c62.h"
#include "c62_remote.h"
#ifdef CONFIG_TETRA
#include "protocols/TETRA/TetraDemod.h"
#include "protocols/TETRA/TetraSpeech.h"
#include "protocols/TETRA/TetraStatus.h"
#endif

/* Raw DesignWare UART registers, as in the baseband dump */
#define UART_BASE DT_REG_ADDR(DT_CHOSEN(zephyr_console))
#define UART_RBR 0x00
#define UART_LSR 0x14
#define LSR_DR BIT(0)

#define CMD_LINE_MAX 64
#define POLL_US 500 /* One character takes 87 us at 115200 baud */
#define THREAD_PRIORITY 6
#define THREAD_STACK_SIZE 2048

volatile bool ui_rtxSyncRequest;

static struct k_thread thread;
static K_THREAD_STACK_DEFINE(thread_stack, THREAD_STACK_SIZE);
static bool started;
static char line[CMD_LINE_MAX];
static size_t lineLen;
static uint32_t autoMs;
static int64_t autoNext;

static void reply(const char *fmt, ...)
{
    char text[CTRL_TEXT_MAX];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof(text))
        n = sizeof(text) - 1;
    baseband_dump_send_text(text, n);
}

static void status(void)
{
    pthread_mutex_lock(&state_mutex);
    uint8_t mode = state.channel.mode;
    uint32_t freq = state.channel.rx_frequency;
    pthread_mutex_unlock(&state_mutex);

    char text[CTRL_TEXT_MAX];
    /* No %lld: newlib-nano's printf fails on long long */
    int n = snprintf(text, sizeof(text),
                     "S t=%lu m=%u f=%lu rssi=%d sv=%lu fo=%ld",
                     (unsigned long)k_uptime_get_32(), mode,
                     (unsigned long)freq, (int)rtx_getRssi(),
                     (unsigned long)state_nvmSaves, (long)c62_getRxOffset());
#ifdef CONFIG_TETRA
    if (mode == OPMODE_TETRA && n > 0 && (size_t)n < sizeof(text)) {
        struct tetraDiag dg;
        struct tetraStatus ts;
        uint32_t frames, bad, otherTs;
        tetra_getDiag(&dg);
        tetra_getStatus(&ts);
        tetraSpeech_stats(&frames, &bad, &otherTs);
        uint32_t played, busy, noStream, noBuffer, bursts, streams;
        tetraSpeech_playStats(&played, &busy, &noStream, &noBuffer, &bursts,
                              &streams);
        n += snprintf(
            text + n, sizeof(text) - n,
            " e=%u cfo=%d clk=%d lk=%d sync=%d crc=%lu ral=%lu "
            "rq=%lu sl=%lu rs=%u ov=%u fr=%lu bad=%lu ots=%lu "
            "cell=%u-%u la=%u seed=%d eq=%d,%d,%d spk=%lu,%lu,%lu,%lu,%lu,%lu",
            dg.phaseErr, dg.carrierHz, dg.clockPpm, dg.locked, ts.sync,
            (unsigned long)dg.crcOk, (unsigned long)dg.realigned,
            (unsigned long)dg.reacquired, (unsigned long)dg.slips, ts.resyncs,
            ts.overruns, (unsigned long)frames, (unsigned long)bad,
            (unsigned long)otherTs, ts.mcc, ts.mnc, ts.la, dg.eqSeed,
            (int)(1000 * dg.eq[3]), (int)(1000 * dg.eq[4]),
            (int)(1000 * dg.eq[5]), (unsigned long)played, (unsigned long)busy,
            (unsigned long)noStream, (unsigned long)noBuffer,
            (unsigned long)bursts, (unsigned long)streams);
    }
#endif
    if (n < 0)
        return;
    if ((size_t)n >= sizeof(text))
        n = sizeof(text) - 1;
    baseband_dump_send_text(text, n);
}

static const struct {
    const char *name;
    uint32_t key;
} keys[] = {
    { "0", KEY_0 },         { "1", KEY_1 },       { "2", KEY_2 },
    { "3", KEY_3 },         { "4", KEY_4 },       { "5", KEY_5 },
    { "6", KEY_6 },         { "7", KEY_7 },       { "8", KEY_8 },
    { "9", KEY_9 },         { "star", KEY_STAR }, { "hash", KEY_HASH },
    { "enter", KEY_ENTER }, { "esc", KEY_ESC },   { "up", KEY_UP },
    { "down", KEY_DOWN },   { "left", KEY_LEFT }, { "right", KEY_RIGHT },
    { "moni", KEY_MONI },   { "f1", KEY_F1 },
};

#ifdef CONFIG_TETRA
static float *param(const char *name)
{
    struct tetraDemodParams *p = &tetraDemodParams;
    if (!strcmp(name, "costAlpha"))
        return &p->costAlpha;
    if (!strcmp(name, "timingKp"))
        return &p->timingKp;
    if (!strcmp(name, "timingKi"))
        return &p->timingKi;
    if (!strcmp(name, "fitAlpha"))
        return &p->fitAlpha;
    if (!strcmp(name, "eqMu"))
        return &p->eqMu;
    if (!strcmp(name, "softScale"))
        return &p->softScale;
    if (!strcmp(name, "eqBiasMu"))
        return &p->eqBiasMu;
    if (!strcmp(name, "fitHold"))
        return &p->fitHold;
    if (!strcmp(name, "acqEnergy"))
        return &p->acqEnergy;
    if (!strcmp(name, "okHold"))
        return &p->okHold;
    if (!strcmp(name, "ctxMu"))
        return &p->ctxMu;
    if (!strcmp(name, "ctxSpan"))
        return &p->ctxSpan;
    return NULL;
}

/* Floats as fixed point: newlib-nano printf has no %f */
static void printParam(const char *name, float v)
{
    reply("p %s %ld.%09ld", name, (long)v, (long)((v - (long)v) * 1e9f));
}
#endif

static void setChannel(int mode, uint32_t freq)
{
    pthread_mutex_lock(&state_mutex);
    if (mode >= 0)
        state.channel.mode = mode;
    if (freq != 0) {
        state.channel.rx_frequency = freq;
        state.channel.tx_frequency = freq;
    }
    pthread_mutex_unlock(&state_mutex);
    ui_rtxSyncRequest = true;
    ui_pushEvent(EVENT_STATUS, 0); /* Redraw */
}

static void execute(char *cmd)
{
    char *argv[4] = { 0 };
    int argc = 0;
    for (char *tok = strtok(cmd, " \t"); tok && argc < 4;
         tok = strtok(NULL, " \t"))
        argv[argc++] = tok;
    if (argc == 0)
        return;

    if (!strcmp(argv[0], "ping")) {
        reply("pong");
    } else if (!strcmp(argv[0], "s")) {
        status();
    } else if (!strcmp(argv[0], "a") && argc == 2) {
        autoMs = strtoul(argv[1], NULL, 0);
        autoNext = k_uptime_get();
        reply("a %lu", (unsigned long)autoMs);
    } else if (!strcmp(argv[0], "f") && argc == 2) {
        uint32_t f = strtoul(argv[1], NULL, 0);
        if (f < 100000000 || f > 1000000000) {
            reply("E f range");
            return;
        }
        setChannel(-1, f);
        reply("f %lu", (unsigned long)f);
    } else if (!strcmp(argv[0], "m") && argc == 2) {
        int mode = -1;
        if (!strcmp(argv[1], "fm"))
            mode = OPMODE_FM;
        else if (!strcmp(argv[1], "m17"))
            mode = OPMODE_M17;
#ifdef CONFIG_TETRA
        else if (!strcmp(argv[1], "t"))
            mode = OPMODE_TETRA;
#endif
        if (mode < 0) {
            reply("E m mode");
            return;
        }
        setChannel(mode, 0);
        reply("m %s", argv[1]);
    } else if (!strcmp(argv[0], "k") && argc >= 2) {
        for (size_t i = 0; i < ARRAY_SIZE(keys); i++) {
            if (strcmp(argv[1], keys[i].name))
                continue;
            kbd_msg_t msg = { .value = 0 };
            msg.keys = keys[i].key;
            msg.long_press = argc == 3 && argv[2][0] == 'l';
            ui_pushEvent(EVENT_KBD, msg.value);
            reply("k %s", argv[1]);
            return;
        }
        reply("E k key");
    } else if (!strcmp(argv[0], "v") && argc == 2) {
        int v = atoi(argv[1]);
        v = v < 0 ? 0 : (v > 15 ? 15 : v);
        pthread_mutex_lock(&state_mutex);
        state.volume = v * 17;
        pthread_mutex_unlock(&state_mutex);
        reply("v %d", v);
#ifdef CONFIG_TETRA
    } else if (!strcmp(argv[0], "p") && argc == 1) {
        static const char *names[] = {
            "costAlpha", "timingKp",  "timingKi", "fitAlpha",
            "eqMu",      "softScale", "eqBiasMu", "fitHold",
            "acqEnergy", "okHold",    "ctxMu",    "ctxSpan",
        };
        for (size_t i = 0; i < ARRAY_SIZE(names); i++)
            printParam(names[i], *param(names[i]));
    } else if (!strcmp(argv[0], "p") && argc == 3) {
        float *p = param(argv[1]);
        if (p == NULL) {
            reply("E p name");
            return;
        }
        *p = strtof(argv[2], NULL);
        printParam(argv[1], *p);
    } else if (!strcmp(argv[0], "r")) {
        bool seed = argc < 2 || argv[1][0] != '0';
        tetra_requestReset(seed);
        reply("r %d", seed);
#endif
    } else if (!strcmp(argv[0], "fo")) {
        if (argc == 2) {
            long hz = strtol(argv[1], NULL, 0);
            if (hz < -20000 || hz > 20000) {
                reply("E fo range");
                return;
            }
            c62_setRxOffset(hz);
        }
        reply("fo %ld", (long)c62_getRxOffset());
    } else if (!strcmp(argv[0], "rr") && argc == 2) {
        unsigned long reg = strtoul(argv[1], NULL, 0);
        if (reg > 0x7f) {
            reply("E rr reg");
            return;
        }
        reply("rr 0x%02lx 0x%04x", reg, BK4819_readReg(&c62_bk4819, reg));
    } else if (!strcmp(argv[0], "rw") && argc == 3) {
        unsigned long reg = strtoul(argv[1], NULL, 0);
        unsigned long val = strtoul(argv[2], NULL, 0);
        if (reg > 0x7f || val > 0xffff) {
            reply("E rw reg");
            return;
        }
        BK4819_writeReg(&c62_bk4819, reg, val);
        reply("rw 0x%02lx 0x%04x", reg, BK4819_readReg(&c62_bk4819, reg));
#ifdef CONFIG_TETRA_AFC
    } else if (!strcmp(argv[0], "afc")) {
        if (argc == 2) {
            pthread_mutex_lock(&state_mutex);
            state.settings.tetraAfcOff = (argv[1][0] == '0');
            pthread_mutex_unlock(&state_mutex);
        }
        reply("afc %d stored=%d", !state.settings.tetraAfcOff,
              (int)state.settings.tetraAfcOffset);
#endif
    } else if (!strcmp(argv[0], "d") && argc == 2) {
        baseband_dump_pause(argv[1][0] == '0');
        reply("d %d", !baseband_dump_paused());
    } else {
        reply("E ?");
    }
}

static void remote_main(void *a, void *b, void *c)
{
    (void)a;
    (void)b;
    (void)c;
    for (;;) {
        while (sys_read32(UART_BASE + UART_LSR) & LSR_DR) {
            char ch = (char)sys_read32(UART_BASE + UART_RBR);
            if (ch == '\n' || ch == '\r') {
                line[lineLen] = '\0';
                if (lineLen > 0)
                    execute(line);
                lineLen = 0;
            } else if (lineLen < CMD_LINE_MAX - 1) {
                line[lineLen++] = ch;
            }
        }
        if (autoMs != 0 && k_uptime_get() >= autoNext) {
            autoNext += autoMs;
            status();
        }
        k_usleep(POLL_US);
    }
}

void c62_remote_init(void)
{
    if (started)
        return;
    started = true;

    /* GPIOA_15: PTT button -> UART2 RXD (alternate function 3) */
    IOMuxManager_PinConfigure(CSK_IOMUX_PAD_A, 15, CSK_IOMUX_FUNC_ALTER3);
    /* Drop whatever the line carried before */
    while (sys_read32(UART_BASE + UART_LSR) & LSR_DR)
        (void)sys_read32(UART_BASE + UART_RBR);

    k_thread_create(&thread, thread_stack, K_THREAD_STACK_SIZEOF(thread_stack),
                    remote_main, NULL, NULL, NULL, THREAD_PRIORITY, 0,
                    K_NO_WAIT);
    k_thread_name_set(&thread, "remote");
    reply("remote ready");
}
