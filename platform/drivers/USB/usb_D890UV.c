/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <interfaces/delays.h>
#include <peripherals/gpio.h>
#include <hwconfig.h>
#include <pthread.h>
#include "usb.h"
#include "monitor.h"

/*
 * USB device on the STM32U575 OTG_FS controller, PA11 (D-) and PA12 (D+).
 * The setup follows the stock firmware:
 * - 0x08051058: 48MHz USB clock from HSI48, PA11/PA12 in AF10 without pulls,
 *   OTG clock on, VDDUSB declared valid, interrupt 73;
 * - 0x08120BD8: CRS trims HSI48 on the USB start-of-frame, reload 47999,
 *   error limit 34, trim 32;
 * - 0x0812A280: full speed, embedded PHY, no VBUS sensing, no DMA.
 */

#define USB_STACK_SIZE 4096

static pthread_t usbThread;
static volatile bool running;

void _Z17OTG_FS_IRQHandlerv()
{
    tud_int_handler(0);
}

/*
 * Send data to the host, waiting for space in the transmit buffer. Must be
 * called from the USB thread, since it keeps the USB stack running while it
 * waits. Data is dropped when no terminal is open or after one second
 * without progress.
 */
void usb_write(const void *data, size_t len)
{
    const uint8_t *ptr = (const uint8_t *)data;
    uint32_t idle = 0;

    while ((len > 0) && tud_cdc_connected()) {
        uint32_t n = tud_cdc_write(ptr, len);
        ptr += n;
        len -= n;

        if (n > 0) {
            idle = 0;
            continue;
        }

        if (++idle > 1000)
            break;

        tud_cdc_write_flush();
        tud_task();
        sleepFor(0, 1);
    }

    tud_cdc_write_flush();
}

static void *usbThreadFunc(void *arg)
{
    (void)arg;

    char line[128];
    size_t pos = 0;

    while (running) {
        tud_task();

        // Assemble command lines for the debug monitor
        while (tud_cdc_available() > 0) {
            int32_t c = tud_cdc_read_char();
            if (c < 0)
                break;

            if ((c == '\r') || (c == '\n')) {
                if (pos > 0) {
                    line[pos] = '\0';
                    monitor_exec(line);
                }

                pos = 0;
            } else if (pos < sizeof(line) - 1) {
                line[pos++] = c;
            }
        }

        monitor_poll();
        sleepFor(0, 1);
    }

    return NULL;
}

void usb_init()
{
    // HSI48 on
    RCC->CR |= RCC_CR_HSI48ON;
    while ((RCC->CR & RCC_CR_HSI48RDY) == 0)
        ;

    // 48MHz clock source: HSI48 (ICLKSEL = 0)
    RCC->CCIPR1 &= ~RCC_CCIPR1_ICLKSEL;

    // CRS: trim HSI48 on the USB start-of-frame
    RCC->APB1ENR1 |= RCC_APB1ENR1_CRSEN;
    __DSB();
    CRS->CFGR = (47999 << CRS_CFGR_RELOAD_Pos) | (34 << CRS_CFGR_FELIM_Pos)
              | CRS_CFGR_SYNCSRC_1;
    CRS->CR = (CRS->CR & ~CRS_CR_TRIM) | (32 << CRS_CR_TRIM_Pos)
            | CRS_CR_AUTOTRIMEN | CRS_CR_CEN;

    // VDDUSB is present
    RCC->AHB3ENR |= RCC_AHB3ENR_PWREN;
    __DSB();
    PWR->SVMCR |= PWR_SVMCR_USV;

    gpio_setMode(GPIOA, 11, ALTERNATE | ALTERNATE_FUNC(10));
    gpio_setMode(GPIOA, 12, ALTERNATE | ALTERNATE_FUNC(10));
    gpio_setOutputSpeed(GPIOA, 11, HIGH);
    gpio_setOutputSpeed(GPIOA, 12, HIGH);

    RCC->AHB2ENR1 |= RCC_AHB2ENR1_OTGEN;
    __DSB();

    NVIC_SetPriority(OTG_FS_IRQn, 14);

    tusb_init();

    running = true;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, USB_STACK_SIZE);
    pthread_create(&usbThread, &attr, usbThreadFunc, NULL);
}

void usb_terminate()
{
    running = false;
    pthread_join(usbThread, NULL);

    NVIC_DisableIRQ(OTG_FS_IRQn);
    RCC->AHB2ENR1 &= ~RCC_AHB2ENR1_OTGEN;
    CRS->CR &= ~(CRS_CR_AUTOTRIMEN | CRS_CR_CEN);
    RCC->APB1ENR1 &= ~RCC_APB1ENR1_CRSEN;
    __DSB();
}
