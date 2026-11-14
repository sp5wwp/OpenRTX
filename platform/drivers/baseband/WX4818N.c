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

/*
 * Tone section setup written by the stock RX entry after the frequency
 * (0x08010C1C, with the settings of the development radio). It also restores
 * the entries changed for transmission.
 */
static const struct regValue fmRxToneSequence[] = {
    { 0x2E, 0x153B }, { 0x2E, 0x2485 }, { 0x2E, 0x558B }, { 0x2E, 0x74EB },
    { 0x2C, 0x3850 }, { 0x2D, 0x2215 }, { 0x2E, 0xE9D9 },
};

/*
 * Register 0x48 by channel bandwidth, written by the stock firmware V1.05 on
 * entering reception and when transmitting (0x0804BCBC, 0x08116480). In
 * reception the wide value raises the noise level read as RSSI by about 4dB.
 */
#define WX4818N_BW_NARROW 0x082E
#define WX4818N_BW_WIDE 0x9826

static const struct regValue fmRxSequence2[] = {
    { 0x1A, 0x64C1 }, { 0x1B, 0x1258 }, { 0x18, 0x0068 }, { 0x53, 0x773F },
    { 0x53, 0x673F }, { 0x53, 0x567F }, { 0x53, 0x467F }, { 0x53, 0x363A },
    { 0x53, 0x263A }, { 0x53, 0x1634 }, { 0x53, 0x00F0 }, { 0x42, 0x46A1 },
    { 0x43, 0x1F4F }, { 0x49, 0x706A }, { 0x49, 0x0000 }, { 0x58, 0xF1F0 },
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

void wx4818n_startFmRx(const struct wx4818n *dev, uint32_t freq, bool wide)
{
    writeSequence(dev, fmRxSequence, ARRAY_SIZE(fmRxSequence));

    wx4818n_writeReg(dev, WX4818N_REG_FREQ_HI, freq >> 16);
    wx4818n_writeReg(dev, WX4818N_REG_FREQ_LO, freq & 0xFFFF);

    writeSequence(dev, fmRxToneSequence, ARRAY_SIZE(fmRxToneSequence));
    wx4818n_writeReg(dev, 0x48, wide ? WX4818N_BW_WIDE : WX4818N_BW_NARROW);
    writeSequence(dev, fmRxSequence2, ARRAY_SIZE(fmRxSequence2));
    if (freq >= WX4818N_UHF_GAIN_EDGE)
        writeSequence(dev, uhfGainTable, ARRAY_SIZE(uhfGainTable));

    if (freq >= WX4818N_BAND_EDGE)
        writeSequence(dev, upperBand, ARRAY_SIZE(upperBand));
    else
        writeSequence(dev, lowerBand, ARRAY_SIZE(lowerBand));

    wx4818n_writeReg(dev, WX4818N_REG_LATCH, WX4818N_MODE_RX);
}

/*
 * FM transmit configuration of the stock firmware V1.05: the analog branch of
 * the PTT handler (0x0811BE8E-0x0811C0D0), recovered by emulating it with the
 * settings and calibration read from the development radio. In order:
 * standby (0x080E2408, 0x080E23B8), frequency, TX mode setup (0x0811586C with
 * tx = 1), mic gain (0x08116480), 0x08115E64, 0x08115C34, tone (0x080E3D98
 * or 0x0811ADB0), 0x08025D04, 0x081163E8, modulation calibration
 * (0x080D5FEC), band settings and TX mode (0x080E3C68).
 */
static const struct regValue fmTxStandby[] = {
    { WX4818N_REG_LATCH, 0x0000 },
    { 0x16, 0x0000 },
    { 0x3F, 0x3232 },
};

/*
 * The second 0x19 write comes from 0x08115DE8, which writes a RAM shadow
 * (0x2001AFC8, 0x0381 after start-up) with bits 5 and 2 changed by its
 * arguments: 0x0381 in the stock TX.
 */
static const struct regValue fmTxSequence[] = {
    { 0x2A, 0x1081 }, { 0x19, 0x0381 }, { 0x24, 0xA192 }, { 0x25, 0x2590 },
    { 0x26, 0x0010 }, { 0x27, 0xA300 }, { 0x32, 0x0600 }, { 0x53, 0x7000 },
    { 0x18, 0x0068 }, { 0x2B, 0x708D }, { 0x19, 0x0381 }, { 0x20, 0x8D90 },
    { 0x21, 0x3B15 }, { 0x22, 0x8D15 }, { 0x23, 0x3B99 }, { 0x1C, 0x80D8 },
    { 0x1D, 0x3F3F }, { 0x1E, 0x80F0 }, { 0x1F, 0x3F27 },
};

static const struct regValue fmTxSequence2[] = {
    { 0x5C, 0x1000 },
    { 0x10, 0xA3A3 },
    { 0x11, 0x0000 },
    { 0x18, 0x0068 },
};

/* No sub-audio tone (0x08127BE8) */
static const struct regValue fmTxNoTone[] = {
    { 0x2E, 0x153B },
    { 0x2E, 0x353B },
    { 0x2F, 0x0A0F },
    { 0x2D, 0x3205 },
};

static const struct regValue fmTxSequence3[] = {
    { 0x18, 0x0068 },
    { 0x1A, 0x64C1 },
    { 0x1B, 0x1258 },
};

/*
 * Register 0x2E holds several values, selected by bits 15:13. Entries 0 and
 * 1 take the CTCSS tone, entry 6 the modulation calibration.
 */
#define WX4818N_REG_TONE 0x2E
#define WX4818N_TONE_IDX(n) ((uint16_t)(n) << 13)

/* Tone register value: Hz * 2^21 / 100000, tone in 0.1Hz (0x080E3D98) */
#define WX4818N_TONE_VALUE(t) \
    ((uint16_t)(((uint32_t)(t) * 2097152U) / 1000000U))

/* Band settings for transmission (0x080E3C68): 0x5F is left alone */
static const struct regValue upperBandTx[] = {
    { 0x5E, 0x0569 },
    { 0x60, 0x0309 },
    { 0x2B, 0x708D },
};

static const struct regValue lowerBandTx[] = {
    { 0x5E, 0x0468 },
    { 0x60, 0x0306 },
    { 0x2B, 0x708D },
};

#define WX4818N_MODE_TX 0x0002

void wx4818n_startFmTx(const struct wx4818n *dev,
                       const struct wx4818nTxConfig *cfg)
{
    writeSequence(dev, fmTxStandby, ARRAY_SIZE(fmTxStandby));

    wx4818n_writeReg(dev, WX4818N_REG_FREQ_HI, cfg->freq >> 16);
    wx4818n_writeReg(dev, WX4818N_REG_FREQ_LO, cfg->freq & 0xFFFF);

    writeSequence(dev, fmTxSequence, ARRAY_SIZE(fmTxSequence));
    wx4818n_writeReg(dev, 0x48,
                     cfg->wide ? WX4818N_BW_WIDE : WX4818N_BW_NARROW);
    writeSequence(dev, fmTxSequence2, ARRAY_SIZE(fmTxSequence2));

    if (cfg->ctcss != 0) {
        uint16_t tone = WX4818N_TONE_VALUE(cfg->ctcss);

        wx4818n_writeReg(dev, WX4818N_REG_TONE, WX4818N_TONE_IDX(0) | tone);
        wx4818n_writeReg(dev, WX4818N_REG_TONE, WX4818N_TONE_IDX(1) | tone);
        // The stock encoder (0x081194F8) also rewrites the modulation
        // calibration entry
        wx4818n_writeReg(dev, WX4818N_REG_TONE,
                         WX4818N_TONE_IDX(6) | (cfg->toneCal & 0x1FFF));
        wx4818n_writeReg(dev, 0x2D, (tone > 0x0ACC) ? 0x3215 : 0x0215);
        wx4818n_writeReg(dev, 0x2F, 0x0A0F);
    } else {
        writeSequence(dev, fmTxNoTone, ARRAY_SIZE(fmTxNoTone));
    }

    writeSequence(dev, fmTxSequence3, ARRAY_SIZE(fmTxSequence3));

    // Modulation calibration (0x080D5FEC, 0x080E3C20)
    wx4818n_writeReg(dev, 0x31, 0x1000 | cfg->modCal);
    wx4818n_writeReg(dev, WX4818N_REG_TONE,
                     WX4818N_TONE_IDX(6) | (cfg->toneCal & 0x1FFF));

    if (cfg->freq >= WX4818N_BAND_EDGE)
        writeSequence(dev, upperBandTx, ARRAY_SIZE(upperBandTx));
    else
        writeSequence(dev, lowerBandTx, ARRAY_SIZE(lowerBandTx));

    /*
     * Flat modulator for a digital baseband, from the stock firmware's DMR TX
     * set-up (emulated key-up on a DMR channel): no voice filter and level
     * control, response flat down to a few Hz (NOTES.md 3.10). 0x48 takes
     * the wide value, twice the gain of stock's digital one; the deviation is
     * then set by 0x31 (modCal).
     */
    if (cfg->baseband) {
        wx4818n_writeReg(dev, 0x2B, 0xF08D);
        wx4818n_writeReg(dev, 0x10, 0xA3E0);
        wx4818n_writeReg(dev, 0x48, WX4818N_BW_WIDE);
        wx4818n_writeReg(dev, WX4818N_REG_TONE, WX4818N_TONE_IDX(6));
    }

    wx4818n_writeReg(dev, WX4818N_REG_LATCH, WX4818N_MODE_TX);
}

void wx4818n_standby(const struct wx4818n *dev)
{
    wx4818n_writeReg(dev, WX4818N_REG_LATCH, WX4818N_MODE_STANDBY);
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
