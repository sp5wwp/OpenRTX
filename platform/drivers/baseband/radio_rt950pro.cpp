/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <hwconfig.h>
#include <interfaces/radio.h>
#include <interfaces/delays.h>
#include <interfaces/nvmem.h>
#include <calibration/calibInfo_RT950Pro.h>
#include "drivers/GPIO/gpio_zephyr.h"
#include "radioUtils.h"
#include "radio_rt950pro.h"
#include "BK4829_tone.h"

/*
 * Radio driver for the Radtel RT-950 Pro: FM and M17, both ways.
 *
 * The radio has two BK4829 transceivers, the first one wired to the VHF LNA
 * and PA and the second to the UHF ones. Each band has a receive and a
 * transmit RF path switch (active high), and the PA drive (APC) is DAC2 on
 * PA5, set from the factory calibration. The transmit sequence follows the
 * vendor firmware (V0.27), with the PA drive ramped up last and taken down
 * first.
 *
 * M17 uses neither chip's audio path as a baseband, the board's audio nets
 * being shared and narrow:
 * - to transmit, the band's chip sends its tone generator, whose gain is the
 *   baseband (see BK4829_tone.h), while the other chip passes the
 *   microphone to the MCU;
 * - to receive, the band's chip is tuned LOW_IF_HZ below the signal and puts
 *   its mixer's output on its AF pin, which the MCU demodulates (see
 *   at32_adc.h).
 */

static constexpr uint32_t LOW_IF_HZ = 8000;
// AF output level of the chip that receives at the IF: enough over the ADC's
// noise, and as little as that beside the speech DAC1 plays on the same net
static constexpr uint16_t LOW_IF_AF_GAIN = 2;
static constexpr uint16_t REG_AF_GAIN_MASK = 0x000F;  // REG_48
static constexpr uint16_t REG_AFC_OFF = 0x0010;       // REG_73
static constexpr uint16_t REG_MIC_GAIN_MASK = 0x001F; // REG_7D
// TX DSP, AF DAC and mic ADC on, no PLL or PA: the mic input to the AF pin
static constexpr uint16_t REG_30_MIC_CODEC = 0x0306;

// PA drive (APC): DAC2 on PA5, 8 bit, with its output buffer. Only the DAC's
// second channel is touched: the first one plays speech (at32_dac.c)
static constexpr uint32_t CRM_APB1EN = 0x4002101C;
static constexpr uint32_t APB1_DAC = BIT(29);
static constexpr uint32_t DAC_CTRL = 0x40007400;
static constexpr uint32_t DAC_CTRL_CH2_MASK = 0xFFFF0000;
static constexpr uint32_t DAC_CTRL_EN2 = BIT(16);
static constexpr uint32_t DAC_DHR8R2 = 0x4000741C;
static const struct gpioPin paDrive = { &GpioA, 5 };

// Ramped up in steps as Ferrite does (https://github.com/FerriteFW/ferrite),
// written at once by the vendor firmware
static constexpr uint8_t APC_RAMP_STEPS = 4;
// Pause between the PA drive going to zero and the chip leaving TX
static constexpr uint32_t PA_OFF_DELAY_MS = 100;

// Power of the calibration's levels: the radio is sold as 10W, 5W and 1W
static constexpr uint32_t CAL_POWER_MW[RT950PRO_CAL_LEVELS] = { 10000, 5000,
                                                                1000 };
// PA gain per calibration level (vendor firmware V0.27, REG_36 table at
// 0x0802B724: 0x7F high, 0x57 mid and low)
static constexpr uint8_t PA_GAIN1[RT950PRO_CAL_LEVELS] = { 7, 2, 2 };
static constexpr uint8_t PA_GAIN2 = 7;

// Microphone gain and deviation of the vendor firmware (V0.27, 0x0801EAF8)
static constexpr uint8_t TX_MIC_GAIN = 0x24;
static constexpr uint16_t TX_DEVIATION = 0x50A;

static const rtxStatus_t *config;   // Pointer to the radio configuration
static enum opstatus radioStatus;   // Current operating status
static const struct BK4829 *rxChip; // Chip currently receiving, or NULL
static enum opmode opMode;          // Operating mode
static bool micCodec;               // The idle chip passes the microphone
static uint16_t micGain;            // Its REG_7D before that
static struct rt950proCalib calib;  // Factory calibration

/** A chip's registers as the driver's start-up left them. */
struct chipDefaults {
    uint16_t afGain; ///< REG_48
    uint16_t afc;    ///< REG_73
};
static struct chipDefaults vhfDefaults;
static struct chipDefaults uhfDefaults;

static const struct BK4829 *getChip(const enum Band band)
{
    return (band == BND_VHF) ? &rt950_bk4829_vhf : &rt950_bk4829_uhf_hf;
}

/**
 * The chip that does not serve a band.
 */
static const struct BK4829 *getOtherChip(const enum Band band)
{
    return (band == BND_VHF) ? &rt950_bk4829_uhf_hf : &rt950_bk4829_vhf;
}

/**
 * A chip's start-up registers.
 */
static const struct chipDefaults &getDefaults(const struct BK4829 *chip)
{
    return (chip == &rt950_bk4829_vhf) ? vhfDefaults : uhfDefaults;
}

/**
 * Switch off the transmit RF paths, the switches are active high.
 */
static void rfPathTxOff()
{
    gpioPin_clear(&rt950_rf_v3t);
    gpioPin_clear(&rt950_rf_u3t);
    gpioPin_clear(&rt950_rf_sw3t);
}

/**
 * Set up the PA drive, at zero.
 */
static void apcInit()
{
    gpioPin_setMode(&paDrive, ANALOG);
    sys_set_bits(CRM_APB1EN, APB1_DAC);
    sys_write32(0, DAC_DHR8R2);
    sys_clear_bits(DAC_CTRL, DAC_CTRL_CH2_MASK);
    sys_set_bits(DAC_CTRL, DAC_CTRL_EN2);
}

/**
 * Set the PA drive.
 *
 * @param value: drive, 0 leaves the finals unbiased.
 */
static void apcSet(uint8_t value)
{
    sys_write32(value, DAC_DHR8R2);
}

/**
 * Get the calibration level whose PA gain suits a power.
 *
 * @param mW: power in mW.
 * @return the level.
 */
static enum rt950proPowerLevel powerLevel(uint32_t mW)
{
    if (mW > CAL_POWER_MW[RT950PRO_POWER_MID])
        return RT950PRO_POWER_HIGH;
    if (mW > CAL_POWER_MW[RT950PRO_POWER_LOW])
        return RT950PRO_POWER_MID;
    return RT950PRO_POWER_LOW;
}

/**
 * Get the PA drive for a frequency and a power from the factory calibration:
 * the calibration point at or below the frequency, as the vendor firmware
 * takes it (V0.27, lookup at 0x0801E2E0), and between the powers of its
 * levels a straight line.
 *
 * @param freq: transmit frequency in Hz.
 * @param mW: power in mW.
 * @return the PA drive.
 */
static uint8_t txDrive(uint32_t freq, uint32_t mW)
{
    const uint32_t mhz = freq / 1000000;
    enum rt950proCalRow row = RT950PRO_CAL_VHF;
    uint32_t point = MIN((MAX(mhz, 130U) - 130U) / 3U, 15U);

    if (mhz >= 400) {
        row = RT950PRO_CAL_UHF;
        point = MIN((mhz - 400U) / 10U, 12U);
    }

    const int32_t high = calib.txPower[RT950PRO_POWER_HIGH][row][point];
    const int32_t mid = calib.txPower[RT950PRO_POWER_MID][row][point];
    const int32_t low = calib.txPower[RT950PRO_POWER_LOW][row][point];
    const int32_t lowMw = CAL_POWER_MW[RT950PRO_POWER_LOW];
    const int32_t midMw = CAL_POWER_MW[RT950PRO_POWER_MID];
    const int32_t highMw = CAL_POWER_MW[RT950PRO_POWER_HIGH];
    const int32_t power = MIN(MAX(static_cast<int32_t>(mW), lowMw), highMw);

    if (power <= midMw)
        return static_cast<uint8_t>(
            low + ((mid - low) * (power - lowMw)) / (midMw - lowMw));

    return static_cast<uint8_t>(
        mid + ((high - mid) * (power - midMw)) / (highMw - midMw));
}

/**
 * Bring the PA up to the power asked on the chip that is keyed.
 *
 * @param chip: chip that transmits.
 */
static void paOn(const struct BK4829 *chip)
{
    const uint8_t drive = txDrive(config->txFrequency, config->txPower);

    bk4829_set_pa_gain(chip, PA_GAIN1[powerLevel(config->txPower)], PA_GAIN2);
    for (uint8_t step = 1; step <= APC_RAMP_STEPS; step++) {
        apcSet(static_cast<uint8_t>((drive * step) / APC_RAMP_STEPS));
        delayMs(1);
    }
}

/**
 * Take the PA down, and give it time before the chip leaves TX.
 */
static void paOff()
{
    if (radioStatus != TX)
        return;

    apcSet(0);
    delayMs(PA_OFF_DELAY_MS);
}

/**
 * Key a chip in FM, with its microphone input as the modulation.
 *
 * @param chip: chip to key.
 * @return zero on success, a negative error code otherwise.
 */
static int fmKeyUp(const struct BK4829 *chip)
{
    int ret = bk4829_set_freq(chip, config->txFrequency);
    if (ret < 0)
        return ret;

    bk4829_set_filter_bandwidth(
        chip, (config->bandwidth == BW_25) ? BK4829_BW_25K : BK4829_BW_12_5K);
    bk4829_disable_ctdcss(chip);
    if (config->txToneEn) {
        bk4829_enable_tx_ctcss(chip, config->txTone);
        bk4829_set_tx_ctcss_gain(chip, calib.ctcssGain);
    }

    bk4829_set_mic_gain(chip, TX_MIC_GAIN);
    bk4829_set_tx_deviation(chip, TX_DEVIATION);
    bk4829_tx_on(chip);
    return 0;
}

/**
 * Put the chip that does not transmit in its mic codec state, at full mic
 * gain: the signal at the MCU is small.
 */
static void micCodecOn()
{
    const struct BK4829 *chip =
        getOtherChip(getBandFromFrequency(config->txFrequency));

    bk4829_write_reg(chip, BK4829_REG_7D, micGain | REG_MIC_GAIN_MASK);
    bk4829_write_reg(chip, BK4829_REG_30, 0);
    bk4829_write_reg(chip, BK4829_REG_30, REG_30_MIC_CODEC);
    bk4829_set_af(chip, BK4829_AF_TYPE_FM);
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

    opMode = OPMODE_NONE;
    micCodec = false;

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

    vhfDefaults = { bk4829_read_reg(&rt950_bk4829_vhf, BK4829_REG_48),
                    bk4829_read_reg(&rt950_bk4829_vhf, BK4829_REG_73) };
    uhfDefaults = { bk4829_read_reg(&rt950_bk4829_uhf_hf, BK4829_REG_48),
                    bk4829_read_reg(&rt950_bk4829_uhf_hf, BK4829_REG_73) };
    micGain = bk4829_read_reg(&rt950_bk4829_vhf, BK4829_REG_7D);

    bk4829_rtx_off(&rt950_bk4829_vhf);
    bk4829_rtx_off(&rt950_bk4829_uhf_hf);
    bk4829_set_af(&rt950_bk4829_vhf, BK4829_AF_TYPE_MUTE);
    bk4829_set_af(&rt950_bk4829_uhf_hf, BK4829_AF_TYPE_MUTE);
}

void radio_terminate()
{
    radio_disableRtx();
}

void radio_setOpmode(const enum opmode mode)
{
    opMode = mode;
}

void radio_rt950pro_micCodec(const bool enable)
{
    micCodec = enable;
    if (enable) {
        micCodecOn();
        return;
    }

    const struct BK4829 *chip =
        getOtherChip(getBandFromFrequency(config->txFrequency));
    bk4829_rtx_off(chip);
    bk4829_set_af(chip, BK4829_AF_TYPE_MUTE);
    bk4829_write_reg(chip, BK4829_REG_7D, micGain);
}

bool radio_checkRxDigitalSquelch()
{
    if (rxChip == NULL)
        return false;

    return bk4829_get_ctcss(rxChip);
}

void radio_enableAfOutput()
{
    // In M17 the AF pin carries the IF, whatever the speaker is given
    if ((rxChip != NULL) && (opMode != OPMODE_M17))
        bk4829_set_af(rxChip, BK4829_AF_TYPE_FM);
}

void radio_disableAfOutput()
{
    if ((rxChip != NULL) && (opMode != OPMODE_M17))
        bk4829_set_af(rxChip, BK4829_AF_TYPE_MUTE);
}

/**
 * Tune a chip on the received frequency and start the reception.
 *
 * @return zero on success, a negative error code otherwise.
 */
static int startRx(const struct BK4829 *chip)
{
    const struct chipDefaults &defaults = getDefaults(chip);

    bk4829_disable_ctdcss(chip);

    if (opMode == OPMODE_M17) {
        // The signal lands LOW_IF_HZ up the chip's mixer output, which goes
        // out on the AF pin; AFC would follow the modulation
        int ret = bk4829_set_freq(chip, config->rxFrequency - LOW_IF_HZ);
        if (ret < 0)
            return ret;

        bk4829_set_filter_bandwidth(chip, BK4829_BW_25K);
        bk4829_rx_on(chip);
        bk4829_write_reg(chip, BK4829_REG_73, defaults.afc | REG_AFC_OFF);
        bk4829_write_reg(chip, BK4829_REG_48,
                         (defaults.afGain & ~REG_AF_GAIN_MASK)
                             | LOW_IF_AF_GAIN);
        bk4829_set_af(chip, BK4829_AF_TYPE_BASEBAND);
        return 0;
    }

    int ret = bk4829_set_freq(chip, config->rxFrequency);
    if (ret < 0)
        return ret;

    bk4829_set_filter_bandwidth(
        chip, (config->bandwidth == BW_25) ? BK4829_BW_25K : BK4829_BW_12_5K);

    if (config->rxToneEn)
        bk4829_enable_rx_ctcss(chip, config->rxTone);

    bk4829_rx_on(chip);
    bk4829_write_reg(chip, BK4829_REG_73, defaults.afc);
    bk4829_write_reg(chip, BK4829_REG_48, defaults.afGain);
    return 0;
}

void radio_enableRx()
{
    const enum Band band = getBandFromFrequency(config->rxFrequency);
    if (band == BND_NONE) {
        radio_disableRtx();
        return;
    }

    const struct BK4829 *chip = getChip(band);
    const struct BK4829 *other = getOtherChip(band);

    // Never a receive path switched on with a transmit one: leave TX first
    paOff();
    bk4829Tone_keyDown();
    rfPathTxOff();
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
}

void radio_enableTx()
{
    if (config->txDisable)
        return;

    const enum Band band = getBandFromFrequency(config->txFrequency);
    if (band == BND_NONE)
        return;

    const struct BK4829 *chip = getChip(band);

    // Break before make: PA drive off, receive paths off, then the TX path
    apcSet(0);
    rfPathIdle();
    bk4829_rtx_off(chip);
    bk4829_set_af(chip, BK4829_AF_TYPE_MUTE);
    rxChip = NULL;

    // A reception on the other band's chip, or anything since the microphone
    // was connected, may have taken the idle chip out of its mic codec state
    if (micCodec)
        micCodecOn();

    gpioPin_set((band == BND_VHF) ? &rt950_rf_v3t : &rt950_rf_u3t);

    // M17's tone settles before this returns: the PA comes up on a steady
    // carrier
    const int ret = (opMode == OPMODE_M17) ?
                        bk4829Tone_keyUp(chip, config->txFrequency) :
                        fmKeyUp(chip);
    if (ret < 0) {
        radio_disableRtx();
        return;
    }

    radioStatus = TX;
    paOn(chip);
}

void radio_disableRtx()
{
    paOff();
    bk4829Tone_keyDown();
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
