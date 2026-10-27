/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/sys_io.h>
#include <rtx/rtx.h>
#include "baseband_dump_c62.h"

/* Build options, see platform/targets/c62/CMakeLists.txt */
#ifndef C62_BASEBAND_DUMP_BAUD
#define C62_BASEBAND_DUMP_BAUD 115200
#endif
/* 1: s16 samples, 2: 12-bit packed samples */
#ifndef C62_BASEBAND_DUMP_FORMAT
#define C62_BASEBAND_DUMP_FORMAT 1
#endif
/*
 * 0: stream, dropping packets when the UART cannot keep up.
 * 1: burst, fill the whole buffer, send it all, then capture again. This
 *    gives gapless captures of BBDUMP_RING_SIZE at any baud rate.
 */
#ifndef C62_BASEBAND_DUMP_BURST
#define C62_BASEBAND_DUMP_BURST 1
#endif

#define BBDUMP_RING_SIZE (1024 * 1024) /* PSRAM; a power of two */
#define BBDUMP_PACKET_SAMPLES 240      /* 5 ms */
#define BBDUMP_HEADER_SIZE 22
#define BBDUMP_CRC_SIZE 2
#define BBDUMP_VERSION 1
#define BBDUMP_FLAG_RESTART BIT(0)

#if C62_BASEBAND_DUMP_FORMAT == 1
#define BBDUMP_PAYLOAD_SIZE (BBDUMP_PACKET_SAMPLES * 2)
#elif C62_BASEBAND_DUMP_FORMAT == 2
#define BBDUMP_PAYLOAD_SIZE (BBDUMP_PACKET_SAMPLES * 3 / 2)
#else
#error "C62_BASEBAND_DUMP_FORMAT must be 1 (s16) or 2 (12-bit packed)"
#endif

#define BBDUMP_PACKET_SIZE \
    (BBDUMP_HEADER_SIZE + BBDUMP_PAYLOAD_SIZE + BBDUMP_CRC_SIZE)

/* Raw DesignWare UART registers, see dw_uart_reg_venus.h */
#define CONSOLE_NODE DT_CHOSEN(zephyr_console)
#define UART_BASE DT_REG_ADDR(CONSOLE_NODE)
#define UART_THR 0x00
#define UART_TFL 0x80 /* TX FIFO level */
#define UART_FIFO_DEPTH 32

#define SENDER_PRIORITY 4 /* Above the codec worker, but it never spins */
#define SENDER_STACK_SIZE 1024

/* REG_43 in use for RX, kept by radio_C62.cpp */
extern volatile uint16_t c62_rx_reg43;

/* Producer (codec worker) writes head, consumer (sender) writes tail */
static uint8_t ring[BBDUMP_RING_SIZE]
    __attribute__((section(".psram_section")));
static volatile uint32_t ring_head;
static volatile uint32_t ring_tail;

static uint8_t packet[BBDUMP_PACKET_SIZE];
static int16_t pending[BBDUMP_PACKET_SAMPLES];
static size_t pending_count;
static uint32_t sample_index; /* Index of pending[0] */
static uint16_t sequence;
static uint16_t next_flags = BBDUMP_FLAG_RESTART;
static bool burst_hold;
static bool active;

static struct k_thread sender;
K_THREAD_STACK_DEFINE(sender_stack, SENDER_STACK_SIZE);

/**
 * CRC-16/CCITT-FALSE: poly 0x1021, init 0xffff, no reflection.
 */
static uint16_t crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xffff;
    while (len--) {
        crc ^= (uint16_t)(*data++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = v & 0xff;
    p[1] = v >> 8;
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v & 0xffff);
    put16(p + 2, v >> 16);
}

#if C62_BASEBAND_DUMP_FORMAT == 2
/**
 * Round a sample to 12 bits, saturating.
 */
static uint16_t to12(int16_t s)
{
    int32_t v = ((int32_t)s + 8) >> 4;
    if (v > 2047)
        v = 2047;
    return (uint16_t)v & 0x0fff;
}
#endif

/**
 * Build a packet from the pending samples and queue it, or drop it if the
 * ring has no room. Codec worker only.
 */
static void emit_packet(void)
{
    uint8_t *p = packet;
    p[0] = 0xa5;
    p[1] = 0x5a;
    p[2] = 0xc3;
    p[3] = 0x3c;
    p[4] = BBDUMP_VERSION;
    p[5] = C62_BASEBAND_DUMP_FORMAT;
    put16(p + 6, sequence);
    put32(p + 8, sample_index);
    put16(p + 12, BBDUMP_PACKET_SAMPLES);
    put16(p + 14, next_flags);
    put16(p + 16, c62_rx_reg43);
    put16(p + 18, (uint16_t)(int16_t)rtx_getRssi());
    put16(p + 20, BBDUMP_PAYLOAD_SIZE);

    uint8_t *payload = p + BBDUMP_HEADER_SIZE;
#if C62_BASEBAND_DUMP_FORMAT == 1
    for (size_t i = 0; i < BBDUMP_PACKET_SAMPLES; i++)
        put16(payload + 2 * i, (uint16_t)pending[i]);
#else
    for (size_t i = 0; i < BBDUMP_PACKET_SAMPLES; i += 2) {
        uint16_t a = to12(pending[i]);
        uint16_t b = to12(pending[i + 1]);
        *payload++ = a & 0xff;
        *payload++ = (a >> 8) | ((b & 0x0f) << 4);
        *payload++ = b >> 4;
    }
#endif
    put16(p + BBDUMP_HEADER_SIZE + BBDUMP_PAYLOAD_SIZE,
          crc16(p + 4, BBDUMP_HEADER_SIZE - 4 + BBDUMP_PAYLOAD_SIZE));

    sequence++;
    sample_index += BBDUMP_PACKET_SAMPLES;

    uint32_t used = ring_head - ring_tail;
    if (burst_hold) {
        /* Burst mode: wait until the previous burst has been sent */
        if (used != 0)
            return;
        burst_hold = false;
        next_flags |= BBDUMP_FLAG_RESTART;
        put16(p + 14, next_flags);
        put16(p + BBDUMP_HEADER_SIZE + BBDUMP_PAYLOAD_SIZE,
              crc16(p + 4, BBDUMP_HEADER_SIZE - 4 + BBDUMP_PAYLOAD_SIZE));
    }
    if (BBDUMP_RING_SIZE - used < BBDUMP_PACKET_SIZE) {
        /* No room: drop it, the receiver sees the gap */
        burst_hold = C62_BASEBAND_DUMP_BURST;
        return;
    }

    uint32_t offset = ring_head % BBDUMP_RING_SIZE;
    size_t first = MIN(BBDUMP_PACKET_SIZE, BBDUMP_RING_SIZE - offset);
    memcpy(&ring[offset], packet, first);
    memcpy(&ring[0], packet + first, BBDUMP_PACKET_SIZE - first);
    compiler_barrier();
    ring_head += BBDUMP_PACKET_SIZE;
    next_flags = 0;
}

bool baseband_dump_recording(void)
{
    return active && !burst_hold;
}

void baseband_dump_restart(void)
{
    /* Start a fresh packet so the flag marks where the capture restarted */
    pending_count = 0;
    next_flags |= BBDUMP_FLAG_RESTART;
}

void baseband_dump_push(const int16_t *samples, size_t count)
{
    if (!active)
        return;
    while (count > 0) {
        size_t n = MIN(count, BBDUMP_PACKET_SAMPLES - pending_count);
        memcpy(&pending[pending_count], samples, n * sizeof(*samples));
        pending_count += n;
        samples += n;
        count -= n;
        if (pending_count == BBDUMP_PACKET_SAMPLES) {
            emit_packet();
            pending_count = 0;
        }
    }
}

/**
 * Move bytes from the ring to the UART TX FIFO. The vendor driver waits for
 * the shift register to empty after every byte, so this writes the FIFO
 * directly, sleeping for about half the time the FIFO takes to empty.
 */
#define SENDER_SLEEP_US \
    MAX(100, UART_FIFO_DEPTH * 10 * 1000000LL / C62_BASEBAND_DUMP_BAUD / 2)

static void sender_main(void *a, void *b, void *c)
{
    (void)a;
    (void)b;
    (void)c;
    for (;;) {
        uint32_t used = ring_head - ring_tail;
        if (used == 0) {
            k_sleep(K_MSEC(1));
            continue;
        }
        uint32_t level = sys_read32(UART_BASE + UART_TFL);
        uint32_t room = level < UART_FIFO_DEPTH ? UART_FIFO_DEPTH - level : 0;
        uint32_t n = MIN(used, room);
        uint32_t tail = ring_tail;
        for (uint32_t i = 0; i < n; i++)
            sys_write32(ring[(tail + i) % BBDUMP_RING_SIZE],
                        UART_BASE + UART_THR);
        compiler_barrier();
        ring_tail = tail + n;
        k_usleep(SENDER_SLEEP_US);
    }
}

static int discard_char(int c)
{
    return c;
}

extern void __printk_hook_install(int (*fn)(int c));
extern void __stdout_hook_install(int (*hook)(int c));

void baseband_dump_init(void)
{
    const struct device *console = DEVICE_DT_GET(CONSOLE_NODE);
    const struct uart_config config = {
        .baudrate = C62_BASEBAND_DUMP_BAUD,
        .parity = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };

    printk("Baseband dump: %u baud, format %d, %s mode. Console muted.\n",
           (unsigned int)C62_BASEBAND_DUMP_BAUD, C62_BASEBAND_DUMP_FORMAT,
           C62_BASEBAND_DUMP_BURST ? "burst" : "stream");
    k_busy_wait(20000); /* Let the message leave at the old baud rate */

    /* Mute everything else that writes to this UART */
    for (int i = 0; i < log_backend_count_get(); i++)
        log_backend_disable(log_backend_get(i));
    __printk_hook_install(discard_char);
    __stdout_hook_install(discard_char);

    uart_configure(console, &config);

    if (active)
        return;
    active = true;
    k_thread_create(&sender, sender_stack, K_THREAD_STACK_SIZEOF(sender_stack),
                    sender_main, NULL, NULL, NULL, SENDER_PRIORITY, 0,
                    K_NO_WAIT);
    k_thread_name_set(&sender, "bbdump");
}
