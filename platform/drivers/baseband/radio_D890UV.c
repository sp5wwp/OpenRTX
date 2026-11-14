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
#include "drivers/baseband/SCT3288.h"
#include "drivers/SPI/spi_stm32.h"
#include "core/utils.h"
#include "hwconfig.h"

static enum opstatus status = OFF;

/*
 * Radio driver for the AnyTone AT-D890UV, FM mode.
 *
 * Transmission follows the stock firmware V1.05 but is built only with
 * CONFIG_D890UV_TX defined (hwconfig.h): without it the transmitter stays
 * powered down (TX_PWR_EN low) whatever is requested.
 */

/*
 * WX4818N GPIOs of the band A chip driving the RF front end, from the stock
 * firmware V1.05 (function at 0x0810D490, branch for band A) and the AnyTone
 * I/O table. The levels of GPIO4 and GPIO7 for band A reception were checked
 * by measuring the RSSI with each setting.
 */
enum frontEndGpio {
    GPIO_UHF_RX = 0, ///< UHF receiver power, high on UHF
    GPIO_RXB_UV = 4, ///< "RX-B U/V", high on UHF for band A
    GPIO_VHF_RX = 6, ///< VHF receiver, high on VHF
    GPIO_RXA_UV = 7, ///< "RX-A U/V", low on UHF for band A
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
#define CAL_ADDR 0x4F00000
#define CAL_BACKUP_ADDR 0x4F40000
#define CAL_BLOCK_B 0x100
#define CAL_BLOCK_SIZE 42

/* Field offsets, in 16-bit words */
enum calField {
    CAL_A_XTAL = 0x00 / 2, ///< Reference correction, 2000 = none
    CAL_MOD = 0x06 / 2,    ///< Modulation, register 0x31 (A: UHF, B: VHF)
    CAL_A_TONE = 0x0C / 2, ///< Modulation, register 0x2E entry 6
    CAL_A_UHF0 = 0x18 / 2, ///< UHF filter tuning at 400MHz
    CAL_A_UHF1 = 0x1A / 2, ///< UHF filter tuning at 460MHz
    CAL_A_UHF2 = 0x1C / 2, ///< UHF filter tuning at 520MHz
    CAL_B_VHF0 = 0x1E / 2, ///< VHF filter tuning at 136MHz
    CAL_B_VHF1 = 0x20 / 2, ///< VHF filter tuning at 174MHz
    CAL_B_VHF2 = 0x22 / 2, ///< VHF filter tuning at 180MHz
};

#define XTAL_CAL_NONE 2000

static uint16_t calA[CAL_BLOCK_SIZE / 2];
static uint16_t calB[CAL_BLOCK_SIZE / 2];

/*
 * Transmitter power, from the same calibration (functions at 0x0803AAF8 and
 * 0x0811A75C). Blocks A (UHF) and B (VHF) hold one APC value per power level
 * in bytes 2 to 5. Further blocks hold, per level, a value for every MHz:
 * 121 bytes from 400MHz (UHF, at +0xC00 + level * 0x100) and 45 bytes from
 * 136MHz (VHF, at +0x1000 + level * 0x100). The APC value is
 *
 *     base[level] + table[level][MHz] - table[level][reference MHz]
 *
 * with reference 440MHz on UHF and 145MHz on VHF. The DAC gets it times 16.
 */
enum calPower {
    CAL_PWR_BASE = 2,           ///< Byte offset of level 0 in blocks A and B
    CAL_PWR_LEVELS = 4,         ///< Power levels
    CAL_PWR_UHF_TABLE = 0xC00,  ///< Offset of the level 0 UHF table
    CAL_PWR_UHF_LEN = 121,      ///< UHF table entries, 400 to 520MHz
    CAL_PWR_UHF_REF = 40,       ///< UHF reference entry, 440MHz
    CAL_PWR_VHF_TABLE = 0x1000, ///< Offset of the level 0 VHF table
    CAL_PWR_VHF_LEN = 45,       ///< VHF table entries, 136 to 180MHz
    CAL_PWR_VHF_REF = 9,        ///< VHF reference entry, 145MHz
};

static uint8_t pwrUhf[CAL_PWR_LEVELS][CAL_PWR_UHF_LEN];
static uint8_t pwrVhf[CAL_PWR_LEVELS][CAL_PWR_VHF_LEN];

/* Defaults if no calibration is found: mid-scale filter tuning */
static bool calValid;

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
    if ((calA[CAL_A_XTAL] == 0xFFFF) || (calB[CAL_B_VHF0] == 0xFFFF))
        return false;

    for (uint8_t i = 0; i < CAL_PWR_LEVELS; i++) {
        spiNand_read(addr + CAL_PWR_UHF_TABLE + i * 0x100, pwrUhf[i],
                     CAL_PWR_UHF_LEN);
        spiNand_read(addr + CAL_PWR_VHF_TABLE + i * 0x100, pwrVhf[i],
                     CAL_PWR_VHF_LEN);
    }

    return true;
}

static void loadCalibration()
{
    spiNand_init();

    calValid = true;
    if (readCalibration(CAL_ADDR))
        return;

    if (readCalibration(CAL_BACKUP_ADDR))
        return;

    // Without calibration there is no safe transmitter power: no TX
    calValid = false;
    setDefaultCalibration();
}

/*
 * Linear interpolation of the filter tuning between two calibration points,
 * equivalent to the tables built by the stock firmware V1.05 (functions at
 * 0x08049FC4 and 0x0804A0A2).
 */
static uint8_t interpolate(uint32_t freq, uint32_t f0, uint32_t f1, uint16_t v0,
                           uint16_t v1)
{
    if (freq <= f0)
        return v0;

    if (freq >= f1)
        return v1;

    int32_t value =
        (int32_t)v0
        + (((int32_t)v1 - (int32_t)v0) * (int32_t)((freq - f0) / 1000))
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
            value = interpolate(freq, 400000000, 460000000, calA[CAL_A_UHF0],
                                calA[CAL_A_UHF1]);
        else
            value = interpolate(freq, 460000000, 520000000, calA[CAL_A_UHF1],
                                calA[CAL_A_UHF2]);

        DAC1->DHR12R2 = (uint32_t)value << 4;
    } else {
        uint8_t value;

        if (freq < 174000000)
            value = interpolate(freq, 136000000, 174000000, calB[CAL_B_VHF0],
                                calB[CAL_B_VHF1]);
        else
            value = interpolate(freq, 174000000, 180000000, calB[CAL_B_VHF1],
                                calB[CAL_B_VHF2]);

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
    uint32_t f10 = freq / 10;
    uint16_t cal = calA[CAL_A_XTAL];
    uint32_t diff = (cal >= XTAL_CAL_NONE) ? (cal - XTAL_CAL_NONE) :
                                             (XTAL_CAL_NONE - cal);
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
#define AF_LEVEL_MIN 0x60 // Level where the output becomes audible
#define AF_LEVEL_MAX 0x7F

/*
 * The low byte of register 0x30 also gates the transmitter audio input: at
 * 0 (volume at minimum) there is no modulation. Stock has 0x5F77 there when
 * it transmits; checked on air.
 */
#define AF_LEVEL_TX 0x77

/*
 * SCT3288 digital board, which carries the FM microphone audio to the
 * modulator. Packets sent by the stock firmware V1.05, captured by running
 * the whole firmware in an emulator (re-tools/fullemu.py, NOTES.md 3.6) and
 * accepted by the chip on the radio. Fields are from the Sicomm "Packet
 * Interface of SCT3258": 0x10 VOCODER_SEL (0x10 analog), 0x1A PROCESS_MODE
 * (0x80 analog), 0x18 WORK_MODE, 0x19 CARRIER_READY, 0x26 ANALOG_SUBAUDIO,
 * 0x2C MOD_GAIN, 0x39 DC_OFFSET, 0x3C ANALOG_CONFIG, 0x3D CTC_PARAM,
 * 0x70 EQUALIZER, 0x78 call start, 0x21 CALL_STOP; codec packets write
 * codec registers.
 */
struct sctPacket {
    uint8_t type;
    uint8_t len;
    uint8_t fields[24];
};

#define SCT_PKT(t, ...)                            \
    {                                              \
        .type = (t),                               \
        .len = sizeof((uint8_t[]){ __VA_ARGS__ }), \
        .fields = { __VA_ARGS__ },                 \
    }

#define CTL SCT3288_PKT_CONTROL
#define DMR SCT3288_PKT_DMR
#define COD SCT3288_PKT_CODEC
#define RD (SCT3288_PKT_READ | SCT3288_PKT_DPMR)

/* Library open and analog set-up, after the interface word (0x080F403C) */
static const struct sctPacket sctStartup[] = {
    SCT_PKT(CTL, 0x2D, 0x04),
    SCT_PKT(COD, 0x0B, 0x02),
    SCT_PKT(COD, 0x0B, 0x82),
    SCT_PKT(DMR, 0x79, 0x41, 0x40, 0x42, 0x44, 0x08),
    SCT_PKT(RD, 0x33),
    SCT_PKT(CTL, 0x10, 0x04),
    SCT_PKT(CTL, 0x10, 0x10),
    SCT_PKT(CTL, 0x70, 0x21, 0x83, 0xBC, 0x3C, 0xB1, 0x41, 0x01, 0x83, 0xC5,
            0x3B, 0xB9),
    SCT_PKT(CTL, 0x70, 0x22, 0xA6, 0xAD, 0x3E, 0x56, 0x40, 0x1A, 0xA6, 0xAD,
            0x3E, 0x3C),
    SCT_PKT(CTL, 0x70, 0x23, 0x84, 0x6B, 0x3C, 0xB1, 0x40, 0x35, 0x84, 0x6C,
            0x3C, 0x7E),
    SCT_PKT(CTL, 0x70, 0x01, 0x83, 0xBC, 0x3C, 0xB1, 0x41, 0x01, 0x83, 0xC5,
            0x3B, 0xB9),
    SCT_PKT(CTL, 0x70, 0x02, 0xA6, 0xAD, 0x3E, 0x56, 0x40, 0x1A, 0xA6, 0xAD,
            0x3E, 0x3C),
    SCT_PKT(CTL, 0x70, 0x03, 0x84, 0x6B, 0x3C, 0xB1, 0x40, 0x35, 0x84, 0x6C,
            0x3C, 0x7E),
    SCT_PKT(DMR, 0x2F, 0x08),
    SCT_PKT(DMR, 0x1D, 0xFF),
    SCT_PKT(DMR, 0x41, 0x00, 0x2F, 0x00, 0x00, 0x00),
    SCT_PKT(DMR, 0x6C, 0x00, 0x00, 0x00, 0x3C, 0x0E, 0x10, 0x0A, 0x28, 0xFF,
            0xFF, 0xFF, 0xFF),
    SCT_PKT(DMR, 0x1C, 0x00, 0x00),
    SCT_PKT(CTL, 0x69, 0x1F, 0x40),
    SCT_PKT(CTL, 0x68, 0x0B, 0xB8),
    SCT_PKT(COD, 0x08, 0x00),
    SCT_PKT(COD, 0x09, 0x00),
    SCT_PKT(COD, 0x11, 0x00),
    SCT_PKT(COD, 0x08, 0x00),
    SCT_PKT(COD, 0x0F, 0x90),
    SCT_PKT(COD, 0x10, 0x1E),
    SCT_PKT(DMR, 0x7A, 0x00, 0x87, 0x02, 0xD0, 0x02, 0xD0, 0x02, 0xD0),
    SCT_PKT(CTL, 0x06, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF),
    SCT_PKT(CTL, 0x6B, 0x08, 0x00, 0x08, 0x00, 0x61, 0xA8, 0x80, 0x00, 0x40,
            0x00, 0x00, 0x00),
    SCT_PKT(DMR, 0x19, 0x00),
    SCT_PKT(DMR, 0x18, 0x00, 0x00, 0x00),
    SCT_PKT(DMR, 0x1A, 0x80),
    SCT_PKT(DMR, 0x26, 0x04, 0x00),
    SCT_PKT(CTL, 0x3D, 0x03, 0x84, 0x02, 0x58, 0x01, 0x01, 0x02, 0x01, 0x07,
            0x06, 0x80, 0xD0, 0x05, 0x00, 0x02, 0x26, 0x00, 0x76, 0x03, 0xB3,
            0x50, 0x00),
    SCT_PKT(CTL, 0x39, 0x08, 0x00, 0x08, 0x00, 0x08, 0x00, 0x01, 0xF4, 0x08,
            0x00, 0xF2, 0x30),
    SCT_PKT(CTL, 0x3C, 0xCF, 0x46, 0x50, 0x05, 0x78, 0x00, 0x00, 0x00, 0x00,
            0x00, 0xBE),
    SCT_PKT(CTL, 0x06, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF),
    SCT_PKT(CTL, 0x6B, 0x08, 0x00, 0x08, 0x00, 0x61, 0xA8, 0x80, 0x00, 0x40,
            0x00, 0x00, 0x00),
    SCT_PKT(DMR, 0x2C, 0x0D, 0xAC),
    SCT_PKT(CTL, 0x73, 0x62, 0x25, 0x01, 0xA8),
    SCT_PKT(DMR, 0x18, 0x00, 0x00, 0x00),
    SCT_PKT(DMR, 0x26, 0x04, 0x00),
};

/* PTT press, before the transmitter set-up (0x0811B308, analog branch) */
static const struct sctPacket sctTxPrepare[] = {
    SCT_PKT(COD, 0x10, 0x1E),
    SCT_PKT(DMR, 0x19, 0x00),
    SCT_PKT(DMR, 0x18, 0x00, 0x00, 0x00),
    SCT_PKT(CTL, 0x39, 0x08, 0x00, 0x08, 0x00, 0x08, 0x00, 0x01, 0xF4, 0x08,
            0x00, 0x10, 0x00),
    SCT_PKT(DMR, 0x18, 0x00, 0x00, 0x00),
    SCT_PKT(DMR, 0x26, 0x04, 0x00),
};

/* PTT press, after the transmitter set-up */
static const struct sctPacket sctTxStart[] = {
    SCT_PKT(DMR, 0x26, 0x00, 0x00),
    SCT_PKT(DMR, 0x18, 0x02, 0x00, 0x00),
    SCT_PKT(COD, 0x10, 0x1E),
    SCT_PKT(DMR, 0x78),
};

/* PTT release, back to reception */
static const struct sctPacket sctTxStop[] = {
    SCT_PKT(DMR, 0x21),
    SCT_PKT(DMR, 0x18, 0x00, 0x00, 0x00),
    SCT_PKT(DMR, 0x26, 0x04, 0x00),
    SCT_PKT(COD, 0x10, 0x1E),
    SCT_PKT(DMR, 0x19, 0x00),
    SCT_PKT(DMR, 0x18, 0x00, 0x00, 0x00),
    SCT_PKT(CTL, 0x39, 0x08, 0x00, 0x08, 0x00, 0x08, 0x00, 0x01, 0xF4, 0x08,
            0x00, 0xF2, 0x30),
    SCT_PKT(DMR, 0x18, 0x00, 0x00, 0x00),
    SCT_PKT(DMR, 0x26, 0x04, 0x00),
};

#undef CTL
#undef DMR
#undef COD
#undef RD

/* Answer times: vocoder switches take up to 3s (Packet Interface, 1.2.7) */
#define SCT_ANSWER_MS 300
#define SCT_VOCODER_MS 3000
#define SCT_READY_MS 3500
#define SCT_SPI_SPEED 5000000

/* Raw interface word sent without packet header (0x08100664), mode 0 */
static const uint8_t sctInterfaceWord[] = { 0x74, 0xA9, 0x01, 0x00, 0x01, 0x00 };

static bool sctReady = false;

static void sctSend(const struct sctPacket *pkt, size_t count)
{
    if (sctReady == false)
        return;

    for (size_t i = 0; i < count; i++) {
        uint32_t timeout = (pkt[i].fields[0] == 0x10) ? SCT_VOCODER_MS :
                                                        SCT_ANSWER_MS;
        sct3288_command(&sct3288, pkt[i].type, pkt[i].fields, pkt[i].len,
                        timeout);
    }
}

/*
 * Power the digital board and set it up for analog FM as the stock
 * firmware does. The speaker amplifier stays off meanwhile, as in stock:
 * with it on, the chip restarted while its codec and vocoder started.
 */
static void sctStart()
{
    gpio_clearPin(SPK_EN);

    gpio_setMode(SCT_SCK, ALTERNATE | ALTERNATE_FUNC(5));
    gpio_setMode(SCT_MISO, ALTERNATE | ALTERNATE_FUNC(5));
    gpio_setMode(SCT_MOSI, ALTERNATE | ALTERNATE_FUNC(5));
    gpio_setOutputSpeed(SCT_SCK, HIGH);
    gpio_setOutputSpeed(SCT_MOSI, HIGH);
    if (spiStm32_init(&sct_spi, SCT_SPI_SPEED, 0) < 0)
        return;

    /*
     * Stock opens the chip, then powers it off and opens it again from the
     * start (the vocoder switches can reset it, reported as ACK_MESSAGE 1).
     */
    for (int pass = 0; pass < 2; pass++) {
        sctReady = false;
        if (pass > 0)
            sct3288_terminate(&sct3288);

        if (sct3288_init(&sct3288) < 0)
            return;

        sct3288_write(&sct3288, sctInterfaceWord, sizeof(sctInterfaceWord));
        if (sct3288_waitAck(&sct3288, 1, SCT_READY_MS) < 0)
            return;

        sctReady = true;
        sctSend(sctStartup, ARRAY_SIZE(sctStartup));
    }

    // Shown in the Info menu
    sct3288_readFwVersion(&sct3288);
}

static uint8_t afLevel = 0x77;

void radio_setVolume(uint8_t volume)
{
    // The level setting is not linear: the audible range is in its top part
    if (volume == 0)
        afLevel = 0;
    else
        afLevel = AF_LEVEL_MIN
                + (uint8_t)(((uint16_t)volume * (AF_LEVEL_MAX - AF_LEVEL_MIN))
                            / 255);
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
    gpio_setPin(MIC_SEL);
    gpio_clearPin(BT_PWR);
    gpio_clearPin(APC_PWR);
    gpio_setMode(AF_SW0, OUTPUT);
    gpio_setMode(AF_SW1, OUTPUT);
    gpio_setMode(AF_SW2, OUTPUT);
    gpio_setMode(MIC_SEL, OUTPUT);
    gpio_setMode(BT_PWR, OUTPUT);
    gpio_setMode(APC_PWR, OUTPUT);

    loadCalibration();
    wx4818n_init(&wx4818nA);

    // The stock initialisation also sets all band B chip GPIOs as low outputs
    for (uint8_t pin = 0; pin < 8; pin++)
        wx4818n_setGpio(&wx4818nB, pin, false);

    sctStart();
}

void radio_terminate()
{
    DAC1->DHR12R1 = 0;
    gpio_clearPin(TX_PWR_EN);
    gpio_clearPin(APC_PWR);
    if (sctReady)
        sct3288_terminate(&sct3288);
    sctReady = false;
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
    DAC1->DHR12R1 = 0;
    gpio_clearPin(TX_PWR_EN);
    gpio_clearPin(APC_PWR);
    gpio_clearPin(BT_PWR);
    gpio_setPin(MIC_SEL);
    gpio_setPin(AF_SW2);
    gpio_clearPin(AF_SW1);
    gpio_clearPin(AF_SW0);

    tune();
    status = RX;
}

/*
 * Transmit frequency ranges of the stock firmware, from the AT-D890UV
 * specifications: 136-174MHz and 400-480MHz.
 */
#define TX_VHF_MIN 136000000U
#define TX_VHF_MAX 174000000U
#define TX_UHF_MIN 400000000U
#define TX_UHF_MAX 480000000U

/*
 * Power levels of the AT-D890UV, in mW: VHF 0.2/2.5/5/7W, UHF 0.2/2.5/5/6W
 * (Low, Mid, High, Turbo). The requested power selects the nearest level:
 * the thresholds are the midpoints between levels.
 */
static uint8_t powerLevel(uint32_t mW)
{
    if (mW < 1350)
        return 0;
    if (mW < 3750)
        return 1;
    if (mW < 6000)
        return 2;

    return 3;
}

static uint16_t apcValue(uint32_t freq, uint8_t level)
{
    const uint8_t *base;
    const uint8_t *table;
    int32_t index;
    int32_t ref;
    int32_t len;

    if (freq >= UHF_MIN_FREQ) {
        base = (const uint8_t *)calA;
        table = pwrUhf[level];
        index = ((int32_t)(freq / 1000000)) - 400;
        ref = CAL_PWR_UHF_REF;
        len = CAL_PWR_UHF_LEN;
    } else {
        base = (const uint8_t *)calB;
        table = pwrVhf[level];
        index = ((int32_t)(freq / 1000000)) - 136;
        ref = CAL_PWR_VHF_REF;
        len = CAL_PWR_VHF_LEN;
    }

    if (index < 0)
        index = 0;
    if (index >= len)
        index = len - 1;

    int32_t apc = (int32_t)base[CAL_PWR_BASE + level] + (int32_t)table[index]
                - (int32_t)table[ref];
    if (apc < 0)
        apc = 0;
    if (apc > 255)
        apc = 255;

    return (uint16_t)apc << 4;
}

static bool txAllowed(uint32_t freq)
{
    if (calValid == false)
        return false;

    return ((freq >= TX_VHF_MIN) && (freq <= TX_VHF_MAX))
        || ((freq >= TX_UHF_MIN) && (freq <= TX_UHF_MAX));
}

/*
 * Band A GPIO3 selects the transmit path, high from 300MHz (0x080E3D38,
 * called at the end of the stock TX sequence).
 */
#define GPIO_TX_UHF 3

/* Band A GPIO1, low in the stock TX and RX front-end setup (0x0810D490) */
#define GPIO_1 1

void radio_enableTx()
{
#ifndef CONFIG_D890UV_TX
    // Transmission not enabled in this build
    gpio_clearPin(TX_PWR_EN);
    status = OFF;
    return;
#endif

    uint32_t freq = config->txFrequency;

    if ((config->txDisable == 1) || (txAllowed(freq) == false)) {
        status = OFF;
        return;
    }

    gpio_clearPin(SPK_EN);

    /*
     * Stock routing (0x081140EC mode 0 with the TX flag set): PE0 high
     * selects the digital board output, PD2 low feeds it to MICIN.
     */
    gpio_clearPin(BT_PWR);
    gpio_setPin(MIC_SEL);
    gpio_clearPin(AF_SW2);
    gpio_clearPin(AF_SW1);
    gpio_clearPin(AF_SW0);

    sctSend(sctTxPrepare, ARRAY_SIZE(sctTxPrepare));

    wx4818n_standby(&wx4818nB);

    // Front end: band path for the TX frequency, receivers off (0x0810D490)
    setFrontEnd(freq);
    wx4818n_setGpio(&wx4818nA, GPIO_UHF_RX, false);
    wx4818n_setGpio(&wx4818nA, GPIO_VHF_RX, false);
    wx4818n_setGpio(&wx4818nA, GPIO_1, false);

    bool uhf = (freq >= UHF_MIN_FREQ);
    struct wx4818nTxConfig txCfg = {
        .freq = correctFrequency(freq),
        .wide = (config->bandwidth == BW_25),
        .ctcss = config->txToneEn ? config->txTone : 0,
        .modCal = (uhf ? calA[CAL_MOD] : calB[CAL_MOD]) & 0x0FFF,
        .toneCal = calA[CAL_A_TONE],
    };

    wx4818n_startFmTx(&wx4818nA, &txCfg);
    wx4818n_setGpio(&wx4818nA, GPIO_TX_UHF, uhf);
    wx4818n_writeReg(&wx4818nA, 0x30, AF_LEVEL_BASE | AF_LEVEL_TX);

    // Power stage, in the stock order: APC supply, PA supply, APC level
    gpio_setPin(APC_PWR);
    gpio_setPin(TX_PWR_EN);
    DAC1->DHR12R1 = apcValue(freq, powerLevel(config->txPower));

    sctSend(sctTxStart, ARRAY_SIZE(sctTxStart));

    tunedFrequency = 0;
    status = TX;
}

static void stopTx()
{
    DAC1->DHR12R1 = 0;
    gpio_clearPin(TX_PWR_EN);
    gpio_clearPin(APC_PWR);
    wx4818n_standby(&wx4818nA);
    sctSend(sctTxStop, ARRAY_SIZE(sctTxStop));
}

void radio_disableRtx()
{
    if (status == TX)
        stopTx();

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
#define RSSI_REF_RAW 310.0f
#define RSSI_REF_DBM -60.0f
rssi_t radio_getRssi()
{
    // Called by the rtx thread at every iteration: keep the board read
    if (sctReady)
        sct3288_drain(&sct3288);

    if (status != RX)
        return -127.0f;

    uint16_t raw = wx4818n_readReg(&wx4818nA, 0x02) & 0x03FF;
    return (((float)raw - RSSI_REF_RAW) / RSSI_COUNTS_PER_DB) + RSSI_REF_DBM;
}

enum opstatus radio_getStatus()
{
    return status;
}
