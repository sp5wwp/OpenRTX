/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <hwconfig.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>
#include "bluetooth_rt950pro.h"

#ifdef CONFIG_BLUETOOTH

LOG_MODULE_REGISTER(bluetooth, LOG_LEVEL_INF);

#define BT_UART DEVICE_DT_GET(DT_ALIAS(bluetooth))

#define RX_BUF_SIZE 256

/* The module needs some time to start after being powered on */
#define MODULE_START_MS 500

#define MONITOR_STACK_SIZE 1024

/* Received data keeps the link considered connected for this time */
#define CONNECTED_TIMEOUT_MS 3000

static const struct device *btUart;
static uint8_t rxData[RX_BUF_SIZE];
static struct ring_buf rxBuf;
static volatile uint32_t rxOverflows;
static volatile int64_t lastRxTime;
static int64_t lastDataTime;
static bool hasData;

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

        uint32_t stored = ring_buf_put(&rxBuf, chunk, len);
        rxOverflows += len - stored;
        lastRxTime = k_uptime_get();
    }
}

/*
 * Monitor: every byte received from a remote device is logged and echoed back,
 * to test the link.
 */
static void monitorTask(void *p1, void *p2, void *p3)
{
    uint8_t buf[32];
    char text[3 * sizeof(buf) + 1];
    (void)p1;
    (void)p2;
    (void)p3;

    while (true) {
        int len = bluetooth_read(buf, sizeof(buf));
        if (len <= 0) {
            k_msleep(10);
            continue;
        }

        for (int i = 0; i < len; i++)
            snprintk(&text[3 * i], 4, "%02x ", buf[i]);

        LOG_INF("RX %d byte(s): %s", len, text);
        bluetooth_write(buf, len);
    }
}

K_THREAD_DEFINE(btMonitor, MONITOR_STACK_SIZE, monitorTask, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, SYS_FOREVER_MS);

int bluetooth_init(void)
{
    const struct device *uart = BT_UART;

    if (!device_is_ready(uart))
        return -ENODEV;

    // Power on the module and wait for it to start
    gpioPin_set(&rt950_bt_pwr);
    int ret = gpioPin_setMode(&rt950_bt_pwr, OUTPUT);
    if (ret < 0)
        return ret;

    k_msleep(MODULE_START_MS);

    btUart = uart;
    ring_buf_init(&rxBuf, sizeof(rxData), rxData);
    rxOverflows = 0;
    hasData = false;

    ret = uart_irq_callback_user_data_set(uart, uartRxCallback, NULL);
    if (ret < 0) {
        btUart = NULL;
        return ret;
    }

    uart_irq_rx_enable(uart);
    k_thread_start(btMonitor);

    LOG_INF("Module enabled");

    return 0;
}

void bluetooth_terminate(void)
{
    if (btUart != NULL)
        uart_irq_rx_disable(btUart);

    btUart = NULL;
    gpioPin_clear(&rt950_bt_pwr);
}

int bluetooth_write(const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;

    if (btUart == NULL)
        return -ENODEV;

    for (size_t i = 0; i < len; i++)
        uart_poll_out(btUart, bytes[i]);

    return (int)len;
}

int bluetooth_read(void *buf, size_t len)
{
    if (btUart == NULL)
        return -ENODEV;

    unsigned int key = irq_lock();
    uint32_t read = ring_buf_get(&rxBuf, (uint8_t *)buf, len);
    int64_t rxTime = lastRxTime;
    irq_unlock(key);

    if (read > 0) {
        hasData = true;
        lastDataTime = rxTime;
    }

    return (int)read;
}

void bluetooth_flush(void)
{
    unsigned int key = irq_lock();
    ring_buf_reset(&rxBuf);
    irq_unlock(key);
}

uint32_t bluetooth_getOverflows(void)
{
    return rxOverflows;
}

bool bluetooth_isConnected(void)
{
    if (btUart == NULL)
        return false;

    // Data waiting to be read also counts as received data
    if (!ring_buf_is_empty(&rxBuf)) {
        hasData = true;
        lastDataTime = lastRxTime;
    }

    return hasData && ((k_uptime_get() - lastDataTime) < CONNECTED_TIMEOUT_MS);
}

#endif /* CONFIG_BLUETOOTH */
