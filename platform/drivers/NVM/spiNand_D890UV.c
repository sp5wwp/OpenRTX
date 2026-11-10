/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "drivers/NVM/spiNand_D890UV.h"
#include "peripherals/gpio.h"
#include "interfaces/delays.h"
#include "hwconfig.h"

enum spiNandCmd {
    CMD_READ_ID    = 0x9F,
    CMD_PAGE_READ  = 0x13,  ///< Load a page into the chip cache
    CMD_GET_FEAT   = 0x0F,
    CMD_READ_CACHE = 0x03,  ///< Read from the chip cache
};

#define FEAT_STATUS    0xC0
#define STATUS_BUSY    0x01
#define PAGE_SHIFT     12
#define PAGE_MASK      0x0FFF
#define BUSY_TIMEOUT   1000    // Status polls, about 1us each

static void sendByte(uint8_t byte)
{
    for (uint8_t i = 0; i < 8; i++) {
        if (byte & 0x80)
            gpio_setPin(NAND_MOSI);
        else
            gpio_clearPin(NAND_MOSI);

        byte <<= 1;
        delayUs(1);
        gpio_setPin(NAND_CLK);
        delayUs(1);
        gpio_clearPin(NAND_CLK);
    }
}

static uint8_t recvByte()
{
    uint8_t byte = 0;

    for (uint8_t i = 0; i < 8; i++) {
        gpio_setPin(NAND_CLK);
        delayUs(1);
        byte <<= 1;
        if (gpio_readPin(NAND_MISO))
            byte |= 1;
        gpio_clearPin(NAND_CLK);
        delayUs(1);
    }

    return byte;
}

static inline void select()
{
    gpio_clearPin(NAND_CS);
    delayUs(1);
}

static inline void deselect()
{
    gpio_setPin(NAND_CS);
    delayUs(1);
}

void spiNand_init()
{
    gpio_setPin(NAND_CS);
    gpio_clearPin(NAND_CLK);
    gpio_clearPin(NAND_MOSI);
    gpio_setMode(NAND_CS, OUTPUT);
    gpio_setMode(NAND_CLK, OUTPUT);
    gpio_setMode(NAND_MOSI, OUTPUT);
    gpio_setMode(NAND_MISO, INPUT_PULL_UP);
}

void spiNand_readId(uint8_t *id)
{
    select();
    sendByte(CMD_READ_ID);
    sendByte(0x00);
    for (uint8_t i = 0; i < 3; i++)
        id[i] = recvByte();
    deselect();
}

static int waitReady()
{
    for (uint32_t i = 0; i < BUSY_TIMEOUT; i++) {
        select();
        sendByte(CMD_GET_FEAT);
        sendByte(FEAT_STATUS);
        uint8_t status = recvByte();
        deselect();

        if ((status & STATUS_BUSY) == 0)
            return status;
    }

    return -1;
}

int spiNand_read(uint32_t addr, void *buf, size_t len)
{
    uint32_t page = addr >> PAGE_SHIFT;
    uint16_t col  = addr & PAGE_MASK;
    uint8_t *dst  = (uint8_t *)buf;

    select();
    sendByte(CMD_PAGE_READ);
    sendByte((page >> 16) & 0xFF);
    sendByte((page >> 8) & 0xFF);
    sendByte(page & 0xFF);
    deselect();

    int status = waitReady();
    if (status < 0)
        return status;

    select();
    sendByte(CMD_READ_CACHE);
    sendByte(col >> 8);
    sendByte(col & 0xFF);
    sendByte(0x00);
    for (size_t i = 0; i < len; i++)
        dst[i] = recvByte();
    deselect();

    return status;
}
