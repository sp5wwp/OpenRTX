/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "drivers/baseband/WX4818N.h"
#include "interfaces/delays.h"
#include "core/utils.h"
#include <pthread.h>

/* The bus is shared by both chips and used by several threads */
static pthread_mutex_t busMutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Register numbers known so far, from the stock firmware. The names describe
 * what the stock firmware does with them; there is no datasheet.
 */
enum wx4818nReg {
    WX4818N_REG_GPIO = 0x52,    ///< GPIO output: pin in 15:12, 0x40, level
    WX4818N_REG_DAC = 0x5C,     ///< GPON DAC: 0x1000 | value << 4
    WX4818N_REG_LATCH = 0x56,   ///< Mode: 0 standby, 1 RX, 2 TX
    WX4818N_REG_FREQ_LO = 0x68, ///< RF frequency in Hz, bits 15:0
    WX4818N_REG_FREQ_HI = 0x69, ///< RF frequency in Hz, bits 31:16
};

/* Half clock period of the serial interface */
#define WX4818N_HALF_CLK_US 1

struct regValue {
    uint8_t reg;
    uint16_t value;
};

/*
 * Power-up sequence of the stock firmware V1.05, function at 0x0805FFE8 and
 * the functions it calls, recovered by emulating it. The frequency writes are
 * left out.
 */
static const struct regValue initSequence[] = {
    { 0x50, 0x0001 },
    { 0x58, 0xF1F0 },
    { 0x40, 0x70E6 },
    { 0x40, 0x60FF },
    { 0x40, 0x50F4 },
    { 0x40, 0x40E8 },
    { 0x40, 0x30DC },
    { 0x40, 0x20CD },
    { 0x40, 0x10C3 },
    { 0x40, 0x00B9 },
    { 0x53, 0x773F },
    { 0x53, 0x673F },
    { 0x53, 0x567F },
    { 0x53, 0x467F },
    { 0x53, 0x363A },
    { 0x53, 0x263A },
    { 0x53, 0x1634 },
    { 0x53, 0x00F0 },
    { 0x42, 0x46A1 },
    { 0x43, 0x1F4F },
    { 0x49, 0x706A },
    { 0x12, 0x20D8 },
    { 0x13, 0x00EE },
    { 0x41, 0x711A },
    { 0x42, 0x4A61 },
    { 0x44, 0x1490 },
    { 0x45, 0x8874 },
    { 0x46, 0x006E },
    { 0x47, 0x27FE },
    { 0x49, 0x0050 },
    { 0x49, 0x1078 },
    { 0x49, 0x7034 },
    { 0x49, 0xB0CC },
    { 0x4D, 0x0000 },
    { 0x57, 0xA008 },
    { 0x5C, 0x0001 },
    { 0x5D, 0x8611 },
    { 0x5E, 0x0768 },
    { 0x5F, 0x0768 },
    { 0x60, 0x0300 },
    { 0x2A, 0x1081 },
    { 0x30, 0x5F6B },
    { 0x11, 0x041A },
    { 0x2B, 0x708D },
    { 0x53, 0x773F },
    { 0x26, 0x6468 },
    { 0x27, 0x6080 },
    { 0x27, 0x199C },
    { 0x18, 0x006E },
    { 0x19, 0x0381 },
    { 0x24, 0xF4AF },
    { 0x25, 0x0B56 },
    { 0x32, 0x22A2 },
    { 0x20, 0x8D15 },
    { 0x21, 0x3B99 },
    { 0x22, 0x8D90 },
    { 0x23, 0x3B15 },
    { 0x1C, 0x8137 },
    { 0x1D, 0x3EE0 },
    { 0x1E, 0x80A8 },
    { 0x1F, 0x3F6F },
    { 0x19, 0x0381 },
    { 0x48, 0x9826 },
    { 0x59, 0x221C },
    { 0x29, 0x1090 },
    { 0x56, 0x0000 },
    { 0x3C, 0x00FC },
    /* GPIO0 to GPIO7 as outputs, low */
    { 0x52, 0x0040 },
    { 0x52, 0x1040 },
    { 0x52, 0x2040 },
    { 0x52, 0x3040 },
    { 0x52, 0x4040 },
    { 0x52, 0x5040 },
    { 0x52, 0x6040 },
    { 0x52, 0x7040 },
};

/*
 * Band-dependent settings of the stock firmware V1.05 (function at
 * 0x080E3C68), switched at 390MHz.
 */
#define WX4818N_BAND_EDGE 390000000U

static const struct regValue upperBand[] = {
    { 0x5E, 0x0569 },
    { 0x5F, 0x0569 },
    { 0x60, 0x0309 },
    { 0x2B, 0x708D },
};

static const struct regValue lowerBand[] = {
    { 0x5E, 0x0468 },
    { 0x5F, 0x0468 },
    { 0x60, 0x0306 },
    { 0x2B, 0x708D },
};

/*
 * Sequence applying new settings, from the stock firmware V1.05 (function at
 * 0x080D655C).
 */
static const struct regValue applySequence[] = {
    { 0x58, 0xF1F0 },
    { WX4818N_REG_LATCH, 0x0001 },
};

static void clockPulse(const struct wx4818n *dev)
{
    gpioPin_set(&dev->clk);
    delayUs(WX4818N_HALF_CLK_US);
    gpioPin_clear(&dev->clk);
    delayUs(WX4818N_HALF_CLK_US);
}

static void writeByte(const struct wx4818n *dev, uint8_t byte)
{
    for (uint8_t i = 0; i < 8; i++) {
        if (byte & 0x80)
            gpioPin_set(&dev->dio);
        else
            gpioPin_clear(&dev->dio);

        byte <<= 1;
        clockPulse(dev);
    }
}

static void busSelect(const struct wx4818n *dev)
{
    gpioPin_clear(&dev->clk);
    delayUs(WX4818N_HALF_CLK_US);
    gpioPin_clear(&dev->cs);
    delayUs(WX4818N_HALF_CLK_US);
}

static void busDeselect(const struct wx4818n *dev)
{
    gpioPin_set(&dev->cs);
    delayUs(WX4818N_HALF_CLK_US);
    gpioPin_clear(&dev->dio);
}

void wx4818n_writeReg(const struct wx4818n *dev, uint8_t reg, uint16_t value)
{
    pthread_mutex_lock(&busMutex);
    busSelect(dev);
    writeByte(dev, (uint8_t)(reg << 1));
    writeByte(dev, value >> 8);
    writeByte(dev, value & 0xFF);
    busDeselect(dev);
    pthread_mutex_unlock(&busMutex);
}

uint16_t wx4818n_readReg(const struct wx4818n *dev, uint8_t reg)
{
    uint16_t value = 0;

    pthread_mutex_lock(&busMutex);
    busSelect(dev);
    writeByte(dev, (uint8_t)(reg << 1) | 0x01);

    gpioPin_setMode(&dev->dio, INPUT);
    for (uint8_t i = 0; i < 16; i++) {
        value <<= 1;
        gpioPin_set(&dev->clk);
        delayUs(WX4818N_HALF_CLK_US);
        if (gpioPin_read(&dev->dio))
            value |= 1;
        gpioPin_clear(&dev->clk);
        delayUs(WX4818N_HALF_CLK_US);
    }
    gpioPin_setMode(&dev->dio, OUTPUT);

    busDeselect(dev);
    pthread_mutex_unlock(&busMutex);
    return value;
}

static void writeSequence(const struct wx4818n *dev, const struct regValue *seq,
                          size_t len)
{
    for (size_t i = 0; i < len; i++)
        wx4818n_writeReg(dev, seq[i].reg, seq[i].value);
}

void wx4818n_init(const struct wx4818n *dev)
{
    gpioPin_set(&dev->cs);
    gpioPin_clear(&dev->clk);
    gpioPin_clear(&dev->dio);
    gpioPin_setMode(&dev->cs, OUTPUT);
    gpioPin_setMode(&dev->clk, OUTPUT);
    gpioPin_setMode(&dev->dio, OUTPUT);

    writeSequence(dev, initSequence, ARRAY_SIZE(initSequence));
}

/*
 * GPIO and DAC write formats from the stock firmware V1.05, functions at
 * 0x08010B2C and 0x0801A35C.
 */
void wx4818n_setGpio(const struct wx4818n *dev, uint8_t pin, bool level)
{
    wx4818n_writeReg(dev, WX4818N_REG_GPIO,
                     ((uint16_t)(pin & 0x0F) << 12) | 0x40 | (level ? 1 : 0));
}

void wx4818n_setDac(const struct wx4818n *dev, uint8_t value)
{
    // The stock firmware applies the new value with the sequence used after
    // every setting change (function at 0x0801A35C)
    wx4818n_writeReg(dev, WX4818N_REG_DAC, 0x1000 | ((uint16_t)value << 4));
    writeSequence(dev, applySequence, ARRAY_SIZE(applySequence));
}

/*
 * FM receive configuration of the stock firmware V1.05 (function at
 * 0x08010C1C), recovered by emulating it. The frequency and the operating
 * mode are written after it.
 */
static const struct regValue fmRxSequence[] = {
    { WX4818N_REG_LATCH, 0x0000 },
    { 0x16, 0x0000 },
    { 0x3F, 0x3232 },
    { 0x2A, 0x1081 },
    { 0x11, 0x041A },
    { 0x2B, 0x708D },
    { 0x53, 0x773F },
    { 0x26, 0x6468 },
    { 0x27, 0x6080 },
    { 0x27, 0x199C },
    { 0x18, 0x006E },
    { 0x19, 0x0381 },
    { 0x24, 0xF4AF },
    { 0x25, 0x0B56 },
    { 0x32, 0x22A2 },
    { 0x20, 0x8D15 },
    { 0x21, 0x3B99 },
    { 0x22, 0x8D90 },
    { 0x23, 0x3B15 },
    { 0x1C, 0x8137 },
    { 0x1D, 0x3EE0 },
    { 0x1E, 0x80A8 },
    { 0x1F, 0x3F6F },
    { 0x30, 0x5F77 },
    { 0x10, 0xA3A3 },
    { 0x11, 0x041A },
    { 0x18, 0x006E },
};

static const struct regValue fmRxSequence2[] = {
    { 0x48, 0x082E }, { 0x1A, 0x64C1 }, { 0x1B, 0x1258 }, { 0x18, 0x0068 },
    { 0x53, 0x773F }, { 0x53, 0x673F }, { 0x53, 0x567F }, { 0x53, 0x467F },
    { 0x53, 0x363A }, { 0x53, 0x263A }, { 0x53, 0x1634 }, { 0x53, 0x00F0 },
    { 0x42, 0x46A1 }, { 0x43, 0x1F4F }, { 0x49, 0x706A }, { 0x49, 0x0000 },
    { 0x58, 0xF1F0 },
};

/*
 * Gain table entries that differ from 300MHz up, from the stock firmware V1.05
 * (function at 0x081155AC).
 */
#define WX4818N_UHF_GAIN_EDGE 300000000U

static const struct regValue uhfGainTable[] = {
    { 0x53, 0x463D },
    { 0x53, 0x2637 },
};

/* Operating modes, register 0x56 */
#define WX4818N_MODE_STANDBY 0x0000
#define WX4818N_MODE_RX 0x0001

void wx4818n_startFmRx(const struct wx4818n *dev, uint32_t freq)
{
    writeSequence(dev, fmRxSequence, ARRAY_SIZE(fmRxSequence));

    wx4818n_writeReg(dev, WX4818N_REG_FREQ_HI, freq >> 16);
    wx4818n_writeReg(dev, WX4818N_REG_FREQ_LO, freq & 0xFFFF);

    writeSequence(dev, fmRxSequence2, ARRAY_SIZE(fmRxSequence2));
    if (freq >= WX4818N_UHF_GAIN_EDGE)
        writeSequence(dev, uhfGainTable, ARRAY_SIZE(uhfGainTable));

    if (freq >= WX4818N_BAND_EDGE)
        writeSequence(dev, upperBand, ARRAY_SIZE(upperBand));
    else
        writeSequence(dev, lowerBand, ARRAY_SIZE(lowerBand));

    wx4818n_writeReg(dev, WX4818N_REG_LATCH, WX4818N_MODE_RX);
}

void wx4818n_setFrequency(const struct wx4818n *dev, uint32_t freq)
{
    /*
     * Frequency in Hz, as written by the stock firmware V1.05 (function at
     * 0x08116674). The stock firmware also applies a reference crystal
     * correction from the radio calibration, not done here. A new frequency
     * is taken only when going from standby to receive.
     */
    wx4818n_writeReg(dev, WX4818N_REG_LATCH, 0x0000);

    if (freq >= WX4818N_BAND_EDGE)
        writeSequence(dev, upperBand, ARRAY_SIZE(upperBand));
    else
        writeSequence(dev, lowerBand, ARRAY_SIZE(lowerBand));

    wx4818n_writeReg(dev, WX4818N_REG_FREQ_HI, freq >> 16);
    wx4818n_writeReg(dev, WX4818N_REG_FREQ_LO, freq & 0xFFFF);
    writeSequence(dev, applySequence, ARRAY_SIZE(applySequence));
}
