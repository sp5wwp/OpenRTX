/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <hwconfig.h>
#include <soc.h>
#include <interfaces/radio.h>
#include <interfaces/delays.h>
#include <interfaces/nvmem.h>
#include <calibration/calibInfo_RT950Pro.h>
#include "bench.h"
#include "canceller.h"
#include "clockcal.h"
#include <drivers/audio/dac_at32.h>
#include "radioUtils.h"

/*
 * Radio driver for the Radtel RT-950 Pro.
 *
 * The radio has two BK4829 transceivers, the first one wired to the VHF LNA
 * and PA and the second to the UHF ones. Each band has a receive and a
 * transmit RF path switch (active high), and the PA drive (APC) is DAC2 on
 * PA5. The transmit sequence follows the vendor firmware (V0.27, see the
 * rt950pro README), with the PA drive ramped up last and taken down first.
 */

static const rtxStatus_t *config;   // Pointer to the radio configuration
static enum opstatus radioStatus;   // Current operating status
static const struct BK4829 *rxChip; // Chip currently receiving, or NULL
static const struct BK4829 *txChip; // Chip currently transmitting, or NULL
static struct rt950proCalib calib;  // Factory calibration

static const struct BK4829 *getChip(const enum Band band)
{
    return (band == BND_VHF) ? &rt950_bk4829_vhf : &rt950_bk4829_uhf_hf;
}

/**
 * Switch off the receive RF paths, the switches are active high.
 */
static void rfPathIdle()
{
    gpioPin_clear(&rt950_rf_v3r);
    gpioPin_clear(&rt950_rf_u3r);
    gpioPin_clear(&rt950_rf_u6r);
}

/**
 * Switch off the transmit RF paths. PE14 (HF) is never switched on here.
 */
static void rfPathTxOff()
{
    gpioPin_clear(&rt950_rf_u3t);
    gpioPin_clear(&rt950_rf_v3t);
    gpioPin_clear(&rt950_rf_sw3t);
}

/*
 * PA drive, DAC2 on PA5, 8 bit. Ramped up in steps as Ferrite does, written at
 * once by the vendor firmware.
 */
static constexpr uint8_t APC_RAMP_STEPS = 4;

static void apcInit()
{
    gpio_init_type pin;

    gpio_default_para_init(&pin);
    pin.gpio_pins = GPIO_PINS_5;
    pin.gpio_mode = GPIO_MODE_ANALOG;
    gpio_init(GPIOA, &pin);

    dac_2_data_set(DAC2_8BIT_RIGHT, 0);
    dac_output_buffer_enable(DAC2_SELECT, TRUE);
    dac_trigger_enable(DAC2_SELECT, FALSE);
    dac_enable(DAC2_SELECT, TRUE);
}

static void apcSet(uint8_t value)
{
    dac_2_data_set(DAC2_8BIT_RIGHT, value);
}

void bench_paOff(void)
{
    apcSet(0);
}

/*
 * PA settings per power level (vendor firmware V0.27, REG_36 table at
 * 0x0802B724: 0x7F high, 0x57 mid and low).
 */
static constexpr uint8_t PA_GAIN1[RT950PRO_CAL_LEVELS] = { 7, 2, 2 };
static constexpr uint8_t PA_GAIN2 = 7;

/* Microphone gain and deviation of the vendor firmware (V0.27, 0x0801EAF8) */
static constexpr uint8_t TX_MIC_GAIN = 0x24;
static constexpr uint16_t TX_DEVIATION = 0x50A;

/* Pause between the PA drive going to zero and the chip leaving TX */
static constexpr uint32_t PA_OFF_DELAY_MS = 100;

static enum rt950proPowerLevel powerLevel(uint32_t mW)
{
    if (mW >= 5000)
        return RT950PRO_POWER_HIGH;
    if (mW >= 2000)
        return RT950PRO_POWER_MID;
    return RT950PRO_POWER_LOW;
}

/**
 * Factory calibration point for a transmit frequency (vendor firmware V0.27,
 * lookup at 0x0801E2E0).
 *
 * @return the PA drive, 0 if the frequency has no programmed point.
 */
static uint8_t txDrive(uint32_t freq, enum rt950proPowerLevel level)
{
    const uint32_t mhz = freq / 1000000;
    enum rt950proCalRow row;
    uint32_t point;

    if (calib.blank == 0xFF)
        return 0;

    if (mhz >= 400) {
        row = RT950PRO_CAL_UHF;
        point = MIN((mhz - 400) / 10, 12U);
    } else if ((mhz >= 130) && (mhz < 200)) {
        row = RT950PRO_CAL_VHF;
        point = MIN((mhz - 130) / 3, 15U);
    } else {
        return 0;
    }

    uint8_t drive = calib.txPower[level][row][point];
    return ((drive == 0x00) || (drive == 0xFF)) ? 0 : drive;
}

/*
 * Bench MCU clock measurement (clockcal.c): the UHF chip, receiving, plays a
 * tone from its TCXO on the audio node; its frequency measured on the audio
 * ticks gives the MCU clock error, which trims the audio sample rate. The
 * HEXT crystal measured -5 ppm on the bench, negligible for the M17 symbol
 * clock, so the radio does not run it by itself. Not while the speaker is
 * on: the tone would be heard.
 */
/* Not a frequency used by the UI beeps, which come from the MCU clock */
static constexpr uint32_t CAL_TONE_HZ = 1777;
static constexpr uint32_t CAL_RX_FREQ = 435000000;

void bench_calibrateClock(void)
{
    if (gpioPin_read(&rt950_amp_en))
        return;

    const struct BK4829 *chip = &rt950_bk4829_uhf_hf;
    const uint16_t word = bk4829_tone_word(CAL_TONE_HZ);
    const bool cancel = canceller_isEnabled();

    canceller_enable(false);
    bk4829_set_freq(chip, CAL_RX_FREQ);
    bk4829_rx_on(chip);
    bk4829_rx_tone_start(chip, word);

    int32_t error;
    int ret = clockcal_measure(bk4829_tone_hz(word), &error);

    bk4829_rx_tone_stop(chip);
    bk4829_rtx_off(chip);
    canceller_enable(cancel);

    if (ret < 0)
        return;

    dacAt32_setClockTrim(error);
}

/**
 * Route the RF path for reception in the given band.
 */
static void rfPathRx(const enum Band band)
{
    /*
     * Sweeps of the switch combinations with a signal in each band:
     * - VHF: V3R high gives about 35dB more level, U3R and U6R have no effect.
     * - UHF: U3R high gives about 35dB more level, U6R high costs about 20dB
     *   and V3R has little effect (a few dB less when high).
     * The switch of the unused band is left low, U6R is always low.
     */
    gpioPin_clear(&rt950_rf_u6r);

    if (band == BND_VHF) {
        gpioPin_set(&rt950_rf_v3r);
        gpioPin_clear(&rt950_rf_u3r);
    } else {
        gpioPin_set(&rt950_rf_u3r);
        gpioPin_clear(&rt950_rf_v3r);
    }
}

void radio_init(const rtxStatus_t *rtxState)
{
    config = rtxState;
    radioStatus = OFF;
    rxChip = NULL;

    txChip = NULL;

    apcInit();
    rfPathIdle();
    rfPathTxOff();

    gpioPin_setMode(&rt950_rf_u6r, OUTPUT);  // HF RX switch
    gpioPin_setMode(&rt950_rf_u3r, OUTPUT);  // UHF RX switch
    gpioPin_setMode(&rt950_rf_v3r, OUTPUT);  // VHF RX switch
    gpioPin_setMode(&rt950_rf_u3t, OUTPUT);  // UHF TX switch
    gpioPin_setMode(&rt950_rf_v3t, OUTPUT);  // VHF TX switch
    gpioPin_setMode(&rt950_rf_sw3t, OUTPUT); // HF TX switch

    nvm_readCalibData(&calib);

    bk4829_rtx_off(&rt950_bk4829_vhf);
    bk4829_rtx_off(&rt950_bk4829_uhf_hf);
    bk4829_set_af(&rt950_bk4829_vhf, BK4829_AF_TYPE_MUTE);
    bk4829_set_af(&rt950_bk4829_uhf_hf, BK4829_AF_TYPE_MUTE);
}

void radio_terminate()
{
    radio_disableRtx();
}

/*
 * M17 receive gains (REG_48), measured on the bench: receiver noise at about
 * 10 ADC counts RMS on the audio node, 20 dB above the ADC noise, so that the
 * baseband stays low at the speaker under the decoded voice played by the
 * DAC, which shares that node.
 */
static constexpr uint8_t M17_RX_AUDIO_GAIN1 = 0;
static constexpr uint8_t M17_RX_AUDIO_GAIN2 = 0x0a;
static constexpr uint8_t M17_AF_DAC_GAIN = 1;

static void setAudioMode(const struct BK4829 *chip, const enum opmode mode)
{
    if (mode == OPMODE_M17) {
        bk4829_set_audio_mode(chip, BK4829_AUDIO_BYPASS);
        bk4829_set_rx_audio_gain(chip, M17_RX_AUDIO_GAIN1, M17_RX_AUDIO_GAIN2,
                                 M17_AF_DAC_GAIN);
    } else {
        bk4829_set_audio_mode(chip, BK4829_AUDIO_VOICE);
    }
}

void radio_setOpmode(const enum opmode mode)
{
    // The core calls this before disabling the old mode
    radio_disableRtx();

    setAudioMode(&rt950_bk4829_vhf, mode);
    setAudioMode(&rt950_bk4829_uhf_hf, mode);
}

bool radio_checkRxDigitalSquelch()
{
    if (rxChip == NULL)
        return false;

    return bk4829_get_ctcss(rxChip);
}

/*
 * AF output requested by the audio routes, applied to the receiving chip now
 * and whenever a reception starts: a route can be connected before it.
 */
static bool afOutput = false;

void radio_enableAfOutput()
{
    afOutput = true;
    if (rxChip != NULL)
        bk4829_set_af(rxChip, BK4829_AF_TYPE_FM);
}

void radio_disableAfOutput()
{
    afOutput = false;
    if (rxChip != NULL)
        bk4829_set_af(rxChip, BK4829_AF_TYPE_MUTE);
}

/**
 * Tune a chip on the received frequency and start the reception.
 *
 * @return zero on success, a negative error code otherwise.
 */
static int startRx(const struct BK4829 *chip)
{
    bk4829_disable_ctdcss(chip);

    int ret = bk4829_set_freq(chip, config->rxFrequency);
    if (ret < 0)
        return ret;

    bk4829_set_filter_bandwidth(
        chip, (config->bandwidth == BW_25) ? BK4829_BW_25K : BK4829_BW_12_5K);

    if (config->rxToneEn)
        bk4829_enable_rx_ctcss(chip, config->rxTone);

    bk4829_rx_on(chip);
    return 0;
}

void radio_enableRx()
{
    const enum Band band = getBandFromFrequency(config->rxFrequency);
    if (band == BND_NONE) {
        radio_disableRtx();
        return;
    }

    // Never a receive path switched on with a transmit one: leave TX first
    if (txChip != NULL)
        radio_disableRtx();

    const struct BK4829 *chip = getChip(band);
    const struct BK4829 *other = (chip == &rt950_bk4829_vhf) ?
                                   &rt950_bk4829_uhf_hf :
                                   &rt950_bk4829_vhf;

    bk4829_rtx_off(other);
    bk4829_set_af(other, BK4829_AF_TYPE_MUTE);
    rfPathRx(band);

    // Allow the RF switches to settle
    delayMs(5);

    if (startRx(chip) < 0) {
        radio_disableRtx();
        return;
    }

    rxChip = chip;
    radioStatus = RX;

    if (afOutput)
        bk4829_set_af(chip, BK4829_AF_TYPE_FM);
}

void radio_enableTx()
{
    const enum Band band = getBandFromFrequency(config->txFrequency);
    if ((config->txDisable != 0) || ((band != BND_VHF) && (band != BND_UHF)))
        return;

    const enum rt950proPowerLevel level = powerLevel(config->txPower);
    uint8_t drive = txDrive(config->txFrequency, level);
    if (drive == 0)
        return;

    // Bench measurements below the lowest calibrated power, never above
    if ((bench_txDrive() != 0) && (bench_txDrive() < drive))
        drive = bench_txDrive();

    const struct BK4829 *chip = getChip(band);

    // Break before make: PA drive off, receive paths off, then the TX path
    radio_disableRtx();
    apcSet(0);
    gpioPin_set((band == BND_VHF) ? &rt950_rf_v3t : &rt950_rf_u3t);

    if (bk4829_set_freq(chip, config->txFrequency) < 0) {
        rfPathTxOff();
        return;
    }

    bk4829_set_filter_bandwidth(
        chip, (config->bandwidth == BW_25) ? BK4829_BW_25K : BK4829_BW_12_5K);
    bk4829_disable_ctdcss(chip);
    if (config->txToneEn)
        bk4829_enable_tx_ctcss(chip, config->txTone);

    bk4829_set_mic_gain(chip, TX_MIC_GAIN);
    bk4829_set_tx_deviation(chip, TX_DEVIATION);
    bk4829_tx_on(chip);
    bk4829_set_pa_gain(chip, PA_GAIN1[level], PA_GAIN2);

    txChip = chip;
    radioStatus = TX;

    if (bench_txDryRun())
        return;

    for (uint8_t step = 1; step <= APC_RAMP_STEPS; step++) {
        apcSet((drive * step) / APC_RAMP_STEPS);
        delayMs(1);
    }
}

void radio_disableRtx()
{
    // PA drive first, and time for the PA to settle before leaving TX
    if (txChip != NULL) {
        apcSet(0);
        delayMs(PA_OFF_DELAY_MS);
        txChip = NULL;
    }

    rfPathTxOff();
    bk4829_rtx_off(&rt950_bk4829_vhf);
    bk4829_rtx_off(&rt950_bk4829_uhf_hf);
    bk4829_set_af(&rt950_bk4829_vhf, BK4829_AF_TYPE_MUTE);
    bk4829_set_af(&rt950_bk4829_uhf_hf, BK4829_AF_TYPE_MUTE);
    rfPathIdle();

    rxChip = NULL;
    radioStatus = OFF;
}

void radio_updateConfiguration()
{
    // Retune and reconfigure the receiver if it is running
    if (radioStatus == RX)
        radio_enableRx();
}

rssi_t radio_getRssi()
{
    if (rxChip == NULL)
        return -121.0f;

    return bk4829_get_rssi(rxChip);
}

enum opstatus radio_getStatus()
{
    return radioStatus;
}
