/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "interfaces/radio.h"
#include "peripherals/gpio.h"
#include "drivers/baseband/WX4818N.h"
#include "interfaces/delays.h"
#include "drivers/NVM/spiNand_D890UV.h"
#include "hwconfig.h"

static enum opstatus status = OFF;

/*
 * Radio driver for the AnyTone AT-D890UV, receive only for now: the
 * transmitter is kept powered down (TX_PWR_EN low) whatever is requested.
 */

/*
 * WX4818N GPIOs of the band A chip driving the RF front end, from the stock
 * firmware V1.05 (function at 0x0810D490, branch for band A) and the AnyTone
 * I/O table. The levels of GPIO4 and GPIO7 for band A reception were checked
 * by measuring the RSSI with each setting.
 */
enum frontEndGpio {
    GPIO_UHF_RX = 0,    ///< UHF receiver power, high on UHF
    GPIO_RXB_UV = 4,    ///< "RX-B U/V", high on UHF for band A
    GPIO_VHF_RX = 6,    ///< VHF receiver, high on VHF
    GPIO_RXA_UV = 7,    ///< "RX-A U/V", low on UHF for band A
};

/*
 * GPIO of the band B chip needed for band A VHF reception, found by measuring
 * the RSSI with each GPIO inverted ("VHF RX C..." in the I/O table).
 */
#define GPIO_B_VHF_RX 5

/* Band edge used by the stock firmware to choose the front end */
#define UHF_MIN_FREQ 300000000U

static const rtxStatus_t *config;

/*
 * Factory calibration, stored in the external flash by the stock firmware
 * V1.05 (function at 0x0813A0F0): two 42-byte blocks, with a backup copy.
 */
#define CAL_ADDR        0x4F00000
#define CAL_BACKUP_ADDR 0x4F40000
#define CAL_BLOCK_B     0x100
#define CAL_BLOCK_SIZE  42

/* Field offsets, in 16-bit words */
enum calField {
    CAL_A_XTAL  = 0x00 / 2, ///< Reference correction, 2000 = none
    CAL_A_UHF0  = 0x18 / 2, ///< UHF filter tuning at 400MHz
    CAL_A_UHF1  = 0x1A / 2, ///< UHF filter tuning at 460MHz
    CAL_A_UHF2  = 0x1C / 2, ///< UHF filter tuning at 520MHz
    CAL_B_VHF0  = 0x1E / 2, ///< VHF filter tuning at 136MHz
    CAL_B_VHF1  = 0x20 / 2, ///< VHF filter tuning at 174MHz
    CAL_B_VHF2  = 0x22 / 2, ///< VHF filter tuning at 180MHz
};

#define XTAL_CAL_NONE 2000

static uint16_t calA[CAL_BLOCK_SIZE / 2];
static uint16_t calB[CAL_BLOCK_SIZE / 2];

/* Defaults if no calibration is found: mid-scale filter tuning */
static void setDefaultCalibration()
{
    calA[CAL_A_XTAL] = XTAL_CAL_NONE;
    calA[CAL_A_UHF0] = calA[CAL_A_UHF1] = calA[CAL_A_UHF2] = 0x80;
    calB[CAL_B_VHF0] = calB[CAL_B_VHF1] = calB[CAL_B_VHF2] = 0x80;
}

static bool readCalibration(uint32_t addr)
{
    spiNand_read(addr, calA, CAL_BLOCK_SIZE);
    spiNand_read(addr + CAL_BLOCK_B, calB, CAL_BLOCK_SIZE);

    // Erased flash: no calibration at this address
    return (calA[CAL_A_XTAL] != 0xFFFF) && (calB[CAL_B_VHF0] != 0xFFFF);
}

static void loadCalibration()
{
    spiNand_init();

    if (readCalibration(CAL_ADDR))
        return;

    if (readCalibration(CAL_BACKUP_ADDR))
        return;

    setDefaultCalibration();
}

/*
 * Linear interpolation of the filter tuning between two calibration points,
 * equivalent to the tables built by the stock firmware V1.05 (functions at
 * 0x08049FC4 and 0x0804A0A2).
 */
static uint8_t interpolate(uint32_t freq, uint32_t f0, uint32_t f1,
                           uint16_t v0, uint16_t v1)
{
    if (freq <= f0)
        return v0;

    if (freq >= f1)
        return v1;

    int32_t value = (int32_t)v0
                  + (((int32_t)v1 - (int32_t)v0)
                     * (int32_t)((freq - f0) / 1000))
                  / (int32_t)((f1 - f0) / 1000);

    if (value < 0)
        value = 0;
    if (value > 255)
        value = 255;

    return value;
}

static void setFilterTuning(uint32_t freq)
{
    if (freq >= UHF_MIN_FREQ) {
        uint8_t value;

        if (freq < 460000000)
            value = interpolate(freq, 400000000, 460000000,
                                calA[CAL_A_UHF0], calA[CAL_A_UHF1]);
        else
            value = interpolate(freq, 460000000, 520000000,
                                calA[CAL_A_UHF1], calA[CAL_A_UHF2]);

        DAC1->DHR12R2 = (uint32_t)value << 4;
    } else {
        uint8_t value;

        if (freq < 174000000)
            value = interpolate(freq, 136000000, 174000000,
                                calB[CAL_B_VHF0], calB[CAL_B_VHF1]);
        else
            value = interpolate(freq, 174000000, 180000000,
                                calB[CAL_B_VHF1], calB[CAL_B_VHF2]);

        wx4818n_setDac(&wx4818nA, value);
    }
}

/*
 * Reference crystal correction of the stock firmware V1.05 (function at
 * 0x08047E10), working in 10Hz units: offset = |cal - 2000| * f / 46014476,
 * added when cal is above 2000.
 */
static uint32_t correctFrequency(uint32_t freq)
{
    uint32_t f10  = freq / 10;
    uint16_t cal  = calA[CAL_A_XTAL];
    uint32_t diff = (cal >= XTAL_CAL_NONE) ? (cal - XTAL_CAL_NONE)
                                          : (XTAL_CAL_NONE - cal);
    uint32_t offset = (uint32_t)(((uint64_t)diff * f10) / 46014476U);

    if (cal >= XTAL_CAL_NONE)
        f10 += offset;
    else
        f10 -= offset;

    return f10 * 10;
}

static void setFrontEnd(uint32_t freq)
{
    bool uhf = (freq >= UHF_MIN_FREQ);

    wx4818n_setGpio(&wx4818nA, GPIO_UHF_RX, uhf);
    wx4818n_setGpio(&wx4818nA, GPIO_VHF_RX, !uhf);
    wx4818n_setGpio(&wx4818nA, GPIO_RXA_UV, !uhf);
    wx4818n_setGpio(&wx4818nA, GPIO_RXB_UV, uhf);

    wx4818n_setGpio(&wx4818nB, GPIO_B_VHF_RX, !uhf);
}

/*
 * Audio output level of the WX4818N, register 0x30 = 0x5F00 | level with
 * level 0 to 0x7F, as in the stock firmware V1.05 (function at 0x080E4154).
 */
#define AF_LEVEL_BASE 0x5F00
#define AF_LEVEL_MIN  0x60  // Level where the output becomes audible
#define AF_LEVEL_MAX  0x7F

static uint8_t afLevel = 0x77;

void radio_setVolume(uint8_t volume)
{
    // The level setting is not linear: the audible range is in its top part
    if (volume == 0)
        afLevel = 0;
    else
        afLevel = AF_LEVEL_MIN
                + (uint8_t)(((uint16_t)volume * (AF_LEVEL_MAX - AF_LEVEL_MIN)) / 255);
    if (status == RX)
        wx4818n_writeReg(&wx4818nA, 0x30, AF_LEVEL_BASE | afLevel);
}

void radio_init(const rtxStatus_t *rtxState)
{
    config = rtxState;

    gpio_clearPin(TX_PWR_EN);

    gpio_clearPin(AF_SW0);
    gpio_clearPin(AF_SW1);
    gpio_clearPin(AF_SW2);
    gpio_setPin(MIC_MUTE);
    gpio_clearPin(BT_PWR);
    gpio_clearPin(APC_PWR);
    gpio_setMode(AF_SW0, OUTPUT);
    gpio_setMode(AF_SW1, OUTPUT);
    gpio_setMode(AF_SW2, OUTPUT);
    gpio_setMode(MIC_MUTE, OUTPUT);
    gpio_setMode(BT_PWR, OUTPUT);
    gpio_setMode(APC_PWR, OUTPUT);

    loadCalibration();
    wx4818n_init(&wx4818nA);

    // The stock initialisation also sets all band B chip GPIOs as low outputs
    for (uint8_t pin = 0; pin < 8; pin++)
        wx4818n_setGpio(&wx4818nB, pin, false);
}

void radio_terminate()
{
    gpio_clearPin(TX_PWR_EN);
    status = OFF;
}

void radio_setOpmode(const enum opmode mode)
{
    (void)mode;
}

bool radio_checkRxDigitalSquelch()
{
    return false;
}

void radio_enableAfOutput()
{
    gpio_setPin(SPK_EN);
}

void radio_disableAfOutput()
{
    gpio_clearPin(SPK_EN);
}

/* Time for the receiver to settle after tuning, with the speaker off */
#define TUNE_SETTLE_MS 30

static uint32_t tunedFrequency;

/*
 * Tune band A to the configured frequency with the stock receive sequence,
 * keeping the speaker amplifier off meanwhile to avoid clicks.
 */
static void tune()
{
    bool spkOn = gpio_readPin(SPK_EN);

    gpio_clearPin(SPK_EN);

    setFrontEnd(config->rxFrequency);
    wx4818n_startFmRx(&wx4818nA, correctFrequency(config->rxFrequency));
    setFilterTuning(config->rxFrequency);
    wx4818n_writeReg(&wx4818nA, 0x30, AF_LEVEL_BASE | afLevel);
    tunedFrequency = config->rxFrequency;

    sleepFor(0, TUNE_SETTLE_MS);
    if (spkOn)
        gpio_setPin(SPK_EN);
}

void radio_enableRx()
{
    /*
     * RX audio routing and transmitter shutdown as in the stock firmware
     * V1.05, function at 0x08010C1C.
     */
    gpio_clearPin(TX_PWR_EN);
    gpio_clearPin(APC_PWR);
    gpio_clearPin(BT_PWR);
    gpio_setPin(MIC_MUTE);
    gpio_setPin(AF_SW2);
    gpio_clearPin(AF_SW1);
    gpio_clearPin(AF_SW0);

    tune();
    status = RX;
}

void radio_enableTx()
{
    /* Transmission not supported yet */
    gpio_clearPin(TX_PWR_EN);
}

void radio_disableRtx()
{
    gpio_clearPin(SPK_EN);
    status = OFF;
}

void radio_updateConfiguration()
{
    // Retune only on a frequency change: this is called after any change of
    // the radio settings
    if ((status == RX) && (config->rxFrequency != tunedFrequency))
        tune();
}

/*
 * RSSI from register 0x02, bits 9:0, as read by the stock firmware V1.05
 * (function at 0x08049E3C). Conversion to dBm measured with a signal
 * generator at 430MHz: 3.95 counts per dB, raw 310 at -60dBm.
 */
#define RSSI_COUNTS_PER_DB 3.95f
#define RSSI_REF_RAW       310.0f
#define RSSI_REF_DBM       -60.0f
rssi_t radio_getRssi()
{
    if (status != RX)
        return -127.0f;

    uint16_t raw = wx4818n_readReg(&wx4818nA, 0x02) & 0x03FF;
    return (((float)raw - RSSI_REF_RAW) / RSSI_COUNTS_PER_DB) + RSSI_REF_DBM;
}

enum opstatus radio_getStatus()
{
    return status;
}
