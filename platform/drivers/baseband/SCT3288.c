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

/* Packet header: start bytes, 2-byte length, type */
static const uint8_t packetStart[] = { 0x84, 0xA9, 0x61 };
#define SCT3288_HEADER_LEN 6
#define SCT3288_MAX_FIELDS 32
#define SCT3288_FIELD_ACK 0x17
#define SCT3288_ACK_OK 0
#define SCT3288_ACK_READY 1

int sct3288_send(const struct sct3288 *dev, uint8_t type, const uint8_t *fields,
                 size_t len)
{
    uint8_t buf[SCT3288_HEADER_LEN + SCT3288_MAX_FIELDS + 1];

    if (len > SCT3288_MAX_FIELDS)
        return -EINVAL;

    memcpy(buf, packetStart, sizeof(packetStart));
    buf[3] = (uint8_t)(len >> 8);
    buf[4] = (uint8_t)len;
    buf[5] = type;
    memcpy(buf + SCT3288_HEADER_LEN, fields, len);

    size_t total = SCT3288_HEADER_LEN + len;
    if ((total % 2) != 0)
        buf[total++] = 0;

    sct3288_write(dev, buf, total);
    return 0;
}

/*
 * Read what the chip has and look for packets whose first field is `field`
 * or ACK_MESSAGE. Stores the byte after the identifier and returns the
 * identifier found, or -1.
 */
static int readAnswer(const struct sct3288 *dev, uint8_t field, uint8_t *value)
{
    uint8_t buf[64];
    size_t n = sct3288_read(dev, buf, sizeof(buf));

    // A read can hold several packets
    size_t i = 0;
    while ((i + SCT3288_HEADER_LEN + 2) <= n) {
        if (memcmp(buf + i, packetStart, sizeof(packetStart)) != 0) {
            i++;
            continue;
        }

        size_t len = ((size_t)buf[i + 3] << 8) | buf[i + 4];
        const uint8_t *f = buf + i + SCT3288_HEADER_LEN;
        if ((len >= 2) && ((f[0] == field) || (f[0] == SCT3288_FIELD_ACK))) {
            *value = f[1];
            return f[0];
        }

        i += SCT3288_HEADER_LEN + len;
    }

    return -1;
}

int sct3288_command(const struct sct3288 *dev, uint8_t type,
                    const uint8_t *fields, size_t len, uint32_t timeoutMs)
{
    sct3288_drain(dev);

    int ret = sct3288_send(dev, type, fields, len);
    if (ret < 0)
        return ret;

    /*
     * Fields are answered with the same field and code 0; codec writes and
     * errors come back as ACK_MESSAGE. ACK_MESSAGE 1 (ready after a reset)
     * is not an answer: the chip sends it while switching vocoder.
     */
    for (uint32_t t = 0; t < timeoutMs; t++) {
        uint8_t value;
        int id = readAnswer(dev, fields[0], &value);

        if ((id == SCT3288_FIELD_ACK) && (value == SCT3288_ACK_READY))
            id = -1;

        if (id >= 0)
            return (value == SCT3288_ACK_OK) ? 0 : -EIO;

        sleepFor(0, 1);
    }

    return -ETIMEDOUT;
}

int sct3288_waitAck(const struct sct3288 *dev, uint8_t code, uint32_t timeoutMs)
{
    for (uint32_t t = 0; t < timeoutMs; t++) {
        uint8_t value;
        if ((readAnswer(dev, SCT3288_FIELD_ACK, &value) >= 0)
            && (value == code))
            return 0;

        sleepFor(0, 1);
    }

    return -ETIMEDOUT;
}

void sct3288_drain(const struct sct3288 *dev)
{
    uint8_t buf[64];

    while (sct3288_read(dev, buf, sizeof(buf)) > 0)
        ;
}
