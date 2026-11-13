/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <string.h>
#include "interfaces/delays.h"
#include "peripherals/spi.h"
#include "drivers/baseband/SCT3288.h"

/*
 * Bring-up of the Sicomm SCT3288 as done by the AnyTone AT-D890UV stock
 * firmware V1.05: power-up at 0x080D6F5C, library reset at 0x080F066C,
 * SPI byte layer at 0x0810387C (write) and 0x08103838 (read), receive
 * service at 0x0810A6DC. Packet format: Sicomm "Packet Interface of
 * SCT3258" application note.
 */

/* Boot status packet: ACK_MESSAGE (field 0x17) = 0x0E (0x0810061C) */
static const uint8_t bootStatus[] = {
    0x84, 0xA9, 0x61, 0x00, 0x02, 0x00, 0x17, 0x0E,
};

#define SCT3288_POWERUP_STEP_MS 100
#define SCT3288_RESET_LOW_MS 50
#define SCT3288_RESET_WAIT_MS 100
#define SCT3288_BOOT_TIMEOUT_MS 1000

/*
 * The stock read transmits the last byte written (0x2001DEEE): keep the same
 * value on the bus while reading.
 */
static uint8_t lastWritten;

static uint8_t transfer(const struct sct3288 *dev, uint8_t out)
{
    uint8_t in = 0;

    spi_transfer(dev->spi, &out, &in, 1);
    return in;
}

int sct3288_init(const struct sct3288 *dev)
{
    gpioPin_clear(&dev->reset);
    gpioPin_clear(&dev->power);
    gpioPin_set(&dev->cs);
    gpioPin_clear(&dev->ctl1);
    gpioPin_clear(&dev->ctl2);
    gpioPin_setMode(&dev->reset, OUTPUT);
    gpioPin_setMode(&dev->power, OUTPUT);
    gpioPin_setMode(&dev->cs, OUTPUT);
    gpioPin_setMode(&dev->ctl1, OUTPUT);
    gpioPin_setMode(&dev->ctl2, OUTPUT);
    gpioPin_setMode(&dev->ready, INPUT);
    gpioPin_setMode(&dev->frame, INPUT);

    // Power-up (0x080D6F5C)
    gpioPin_set(&dev->power);
    sleepFor(0, SCT3288_POWERUP_STEP_MS);
    gpioPin_set(&dev->reset);
    sleepFor(0, SCT3288_POWERUP_STEP_MS);
    gpioPin_clear(&dev->reset);
    sleepFor(0, SCT3288_POWERUP_STEP_MS);
    gpioPin_set(&dev->reset);

    // Library reset before opening (0x080F066C)
    gpioPin_clear(&dev->reset);
    sleepFor(0, SCT3288_RESET_LOW_MS);
    gpioPin_set(&dev->reset);
    sleepFor(0, SCT3288_RESET_WAIT_MS);

    lastWritten = 0;

    uint8_t buf[sizeof(bootStatus)];
    for (uint32_t t = 0; t < SCT3288_BOOT_TIMEOUT_MS; t++) {
        size_t n = sct3288_read(dev, buf, sizeof(buf));
        if ((n == sizeof(buf)) && (memcmp(buf, bootStatus, n) == 0))
            return 0;

        sleepFor(0, 1);
    }

    return -ETIMEDOUT;
}

void sct3288_terminate(const struct sct3288 *dev)
{
    /*
     * Power-off as stock (0x080D7964), then leave the control lines
     * floating instead of driving the chip select high: a high line keeps
     * the unpowered chip partly supplied, and it then fails to boot again.
     */
    gpioPin_clear(&dev->power);
    gpioPin_clear(&dev->ctl2);
    gpioPin_clear(&dev->ctl1);
    gpioPin_clear(&dev->reset);
    gpioPin_setMode(&dev->cs, INPUT);
    gpioPin_setMode(&dev->reset, INPUT);
    gpioPin_setMode(&dev->ctl1, INPUT);
    gpioPin_setMode(&dev->ctl2, INPUT);
}

void sct3288_write(const struct sct3288 *dev, const void *data, size_t len)
{
    const uint8_t *ptr = (const uint8_t *)data;

    gpioPin_clear(&dev->cs);
    for (size_t i = 0; i < len; i++) {
        lastWritten = ptr[i];
        transfer(dev, ptr[i]);
    }
    gpioPin_set(&dev->cs);
}

/*
 * Two receive modes, as in the stock receive service (0x0810A6DC): while the
 * frame line is high, take a byte each time the ready line is high, waiting
 * up to 200ms for each; otherwise read while the ready line is high (used
 * for the boot status).
 */
#define SCT3288_BYTE_TIMEOUT_MS 200

size_t sct3288_read(const struct sct3288 *dev, void *buf, size_t maxLen)
{
    uint8_t *ptr = (uint8_t *)buf;
    size_t len = 0;

    if (gpioPin_read(&dev->frame) != 0) {
        uint32_t wait = 0;

        gpioPin_clear(&dev->cs);
        while ((len < maxLen) && (gpioPin_read(&dev->frame) != 0)) {
            if (gpioPin_read(&dev->ready) != 0) {
                ptr[len++] = transfer(dev, lastWritten);
                wait = 0;
                continue;
            }

            if (++wait > SCT3288_BYTE_TIMEOUT_MS)
                break;

            sleepFor(0, 1);
        }
        gpioPin_set(&dev->cs);

        return len;
    }

    if (gpioPin_read(&dev->ready) == 0)
        return 0;

    gpioPin_clear(&dev->cs);
    while ((len < maxLen) && (gpioPin_read(&dev->ready) != 0))
        ptr[len++] = transfer(dev, lastWritten);
    gpioPin_set(&dev->cs);

    return len;
}
