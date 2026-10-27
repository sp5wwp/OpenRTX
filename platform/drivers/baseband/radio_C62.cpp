/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <interfaces/nvmem.h>

#include <hwconfig.h>
#include <interfaces/nvmem.h>
#include <interfaces/radio.h>
#include <interfaces/delays.h>
#include <core/utils.h>

#include <algorithm>
#include <string>

#include "drivers/baseband/BK4819.h"
#include "radioUtils.h"
#ifdef C62_BASEBAND_DUMP
#include "../audio/baseband_dump_c62.h"
#endif

#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>

/**
 * Set TX power using PWM on pin A03
 * PWM Period: 1000us (1kHz), adjustable duty cycle for power level
 *
 * @param power_percent: 0-100 (0 = min power, 100 = max power)
 */
static void set_tx_power(uint8_t power_percent)
{
    //TODO: Check if this works... May need to adjust mapping of
    // power_percent to duty cycle based on actual power output
    // vs duty cycle curve of the PA, and also consider frequency
    // dependence of PA efficiency

    // Calculate duty cycle (1000us period = 1kHz)
    uint32_t period_us = 1000; // 1kHz PWM frequency
    uint32_t pulse_us = (period_us * power_percent) / 100;

    int ret = pwm_set_dt(&pwm_rf_apc, PWM_USEC(period_us), PWM_USEC(pulse_us));

    if (ret < 0) {
        printk("Failed to set TX power PWM: %d\n", ret);
    }
}

static const rtxStatus_t
    *config; // Pointer to data structure with radio configuration

static enum opstatus radioStatus; // Current operating status
static enum opmode radioMode;     // Current operating mode

/*
 * BK4819 REG_43 for M17 RX, written whole (see the V3 register list):
 *   <14:12> RF filter bandwidth, 7 = 4.5 kHz (the widest)
 *   <11:9>  RF filter bandwidth used when the signal is weak, same table.
 *           Leaving it narrower than <14:12> makes the chip narrow the
 *           channel filter at low SNR, so set it to the same value.
 *   <8:6>   AF TX LPF2, TX only
 *   <5:4>   BW mode: 0 = 12.5 kHz, 1 = 6.25 kHz, 2 = 25 kHz. <5> doubles
 *           the RF filter bandwidths and the IF (REG_3D).
 *   <3>     undocumented, 1 in every known firmware
 *   <2>     +6 dB after FM demodulation
 *
 * 0x7808: 4.5 kHz / weak 3.75 kHz, 12.5 kHz mode (M17 default)
 * 0x7828: 4.5 kHz / weak 3.75 kHz, 25 kHz mode (doubled)
 * 0x7e28: 4.5 kHz / weak 4.5 kHz,  25 kHz mode (widest documented setting)
 */
#ifdef C62_WIDE_RX
#define C62_M17_RX_REG43 0x7e28
#else
#define C62_M17_RX_REG43 0x7808
#endif

/* Read by the baseband dump: the REG_43 value currently in use for RX */
extern "C" {
volatile uint16_t c62_rx_reg43 = C62_M17_RX_REG43;
}

#ifdef C62_REG43_SWEEP
/*
 * Cycle REG_43 through these values in M17 RX, C62_REG43_SWEEP_MS each, to
 * measure them with a fixed test signal. Each dump packet carries the value
 * in use. The last three have undocumented bits set. The sweep clock only
 * runs while the dump records, so with the default dwell one 10.4 s burst
 * covers all seven values.
 */
static const uint16_t reg43Sweep[] = {
    0x7808, // M17 default, 12.5 kHz
    0x7828, // 25 kHz, weak-signal filter 3.75 kHz
    0x7e28, // 25 kHz, weak-signal filter 4.5 kHz
    0x7e2c, // as above, +6 dB after demodulation
    0x7e38, // BW mode 3 (undocumented)
    0x7e20, // bit 3 cleared (undocumented)
    0xfe28, // bit 15 set (undocumented)
};
#ifndef C62_REG43_SWEEP_MS
#define C62_REG43_SWEEP_MS 1400
#endif
static size_t reg43SweepIndex;
static int64_t reg43SweepLast;    // Uptime at the previous check
static int64_t reg43SweepElapsed; // Recorded time on the current value
static bool reg43SweepStarted;
#endif

/*
 * C62 BK4819 fields for M17, on top of bk4819_setFlatAudio(): RX and TX
 * values, and the bits that keep their FM value in TX. M17 RX uses AF bypass,
 * REG_47 bit 1 and RX gain2 0x3f, see
 * https://wiki.m17foundation.org/index.php?title=A36Plus
 * The FM values are saved when entering M17 and restored when leaving.
 */
static const struct {
    bk4819_reg_t reg;
    uint16_t mask;
    uint16_t rx;
    uint16_t tx;
    uint16_t txFm;
} m17Fields[] = {
    // TX deviation on, raw value 0x582 (not Hz) for the TX DAC gain in
    // hwconfig.h
    { BK4819_REG_40, 0x1fff, 0x1582, 0x1582, 0x0000 },
    // TX: 4.5 kHz RF / 3.75 kHz weak-signal filter, 12.5 kHz channel.
    // RX: overwritten with c62_rx_reg43 by applyM17Fields()
    { BK4819_REG_43, 0x7ffc, 0x7808, 0x7808, 0x0000 },
    { BK4819_REG_47, 0x0002, 0x0002, 0x0000, 0x0002 }, // Bit 1
    // RX gain1 -6 dB, RX gain2 0x3f
    { BK4819_REG_48, 0x0ff0, 0x07f0, 0x0400, 0x03f0 },
    { BK4819_REG_7D, 0x001f, 0x0000, 0x0000, 0x0000 }, // Minimum mic gain
    { BK4819_REG_7E, 0x8000, 0x0000, 0x8000, 0x0000 }, // AGC fix mode in TX
};

static uint16_t fmFields[ARRAY_SIZE(m17Fields)];

/**
 * Digital modes receive through the flat, wide M17 audio path: M17, and
 * TETRA (receive only).
 */
static bool flatMode(enum opmode mode)
{
    return (mode == OPMODE_M17) || (mode == OPMODE_TETRA);
}

/**
 * Apply the M17 audio settings for receiving or transmitting.
 *
 * @param transmit: true for TX, false for RX.
 */
static void applyM17Fields(bool transmit)
{
    bk4819_setFlatAudio(&c62_bk4819, true);

    for (size_t i = 0; i < ARRAY_SIZE(m17Fields); i++) {
        const auto &field = m17Fields[i];
        uint16_t value = BK4819_readReg(&c62_bk4819, field.reg) & ~field.mask;
        value |= transmit ? field.tx | (fmFields[i] & field.txFm) : field.rx;
        BK4819_writeReg(&c62_bk4819, field.reg, value);
    }

    if (!transmit)
        BK4819_writeReg(&c62_bk4819, BK4819_REG_43, c62_rx_reg43);

#ifdef C62_TETRA_IF_DC_FILTER
    /*
     * TETRA receive: re-enable the RX IF DC filter (REG_7E<2:0>, default
     * 0b110) that the flat audio mode bypasses. With it off, the IF DC
     * offset of this low-IF receiver reaches the demodulator as a tone at
     * the IF (16.9 kHz in 25 kHz mode), one-sided: it distorts the +3pi/4
     * symbols much more than the -3pi/4 ones. The wanted signal is centred
     * on the IF, so filtering DC at the IF input does not touch it.
     */
    if (!transmit && radioMode == OPMODE_TETRA) {
        uint16_t value = BK4819_readReg(&c62_bk4819, BK4819_REG_7E);
        BK4819_writeReg(&c62_bk4819, BK4819_REG_7E, (value & ~0x0007) | 0x0006);
    }
#endif
}

/* The BK4819 GPIOs to control the C62 LNA and PA */
enum {
    GPIO_VHF_RX_LNA = 0,
    GPIO_UHF_RX_LNA,
    GPIO_VHF_TX_PA,
    GPIO_UHF_TX_PA,
    GPIO_ALC_TX_LED
};

void radio_init(const rtxStatus_t *rtxState)
{
    config = rtxState;
    radioStatus = OFF;
    radioMode = OPMODE_FM;

    BK4819_SetAF(&c62_bk4819, 0);

    gpioDev_setMode(&c62_bk4819_gpio, GPIO_VHF_RX_LNA, OUTPUT_ACTIVE_LOW);
    gpioDev_setMode(&c62_bk4819_gpio, GPIO_UHF_RX_LNA, OUTPUT_ACTIVE_LOW);
    gpioDev_setMode(&c62_bk4819_gpio, GPIO_VHF_TX_PA, OUTPUT);
    gpioDev_setMode(&c62_bk4819_gpio, GPIO_UHF_TX_PA, OUTPUT);
    gpioDev_setMode(&c62_bk4819_gpio, GPIO_ALC_TX_LED, OUTPUT);
}

void radio_terminate()
{
}

void radio_tuneVcxo(const int16_t vhfOffset, const int16_t uhfOffset)
{
    (void)vhfOffset;
    (void)uhfOffset;
}

void radio_setOpmode(const enum opmode mode)
{
    if (mode == radioMode)
        return;

    // The core calls this before disabling the old mode: unkey first
    radio_disableRtx();
    radio_disableAfOutput();

    if (flatMode(radioMode)) {
        for (size_t i = 0; i < ARRAY_SIZE(m17Fields); i++) {
            const auto &field = m17Fields[i];
            uint16_t value = BK4819_readReg(&c62_bk4819, field.reg);
            BK4819_writeReg(&c62_bk4819, field.reg,
                            (value & ~field.mask) | fmFields[i]);
        }
        bk4819_setFlatAudio(&c62_bk4819, false);
    }

    // Save the FM values; radio_enableRx/Tx() apply the M17 ones
    if (flatMode(mode)) {
        for (size_t i = 0; i < ARRAY_SIZE(m17Fields); i++) {
            const auto &field = m17Fields[i];
            fmFields[i] = BK4819_readReg(&c62_bk4819, field.reg) & field.mask;
        }
    }

    radioMode = mode;
}

bool radio_checkRxDigitalSquelch()
{
    return bk4819_get_ctcss(&c62_bk4819);
}

void radio_enableAfOutput()
{
    if (flatMode(radioMode))
        BK4819_SetAF(&c62_bk4819, BK4819_AF_TYPE_BYPASS);
    else
        bk4819_set_modulation(&c62_bk4819, true);
}

void radio_disableAfOutput()
{
    BK4819_SetAF(&c62_bk4819, 0);
}

void radio_enableRx()
{
    gpioDev_clear(&c62_bk4819_gpio, GPIO_VHF_RX_LNA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_UHF_RX_LNA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_VHF_TX_PA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_UHF_TX_PA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_ALC_TX_LED);

    //Before enable RX path delay to allow hardware to settle
    delayMs(5);

    if (config->rxFrequency < 174000000) {
        gpioDev_set(&c62_bk4819_gpio, GPIO_VHF_RX_LNA);
    } else {
        gpioDev_set(&c62_bk4819_gpio, GPIO_UHF_RX_LNA);
    }

    bk4819_set_freq(&c62_bk4819, config->rxFrequency);

    if (flatMode(radioMode)) {
        applyM17Fields(false);
    } else {
        bk4819_SetFilterBandwidth(&c62_bk4819, config->bandwidth);
        if (config->rxToneEn)
            bk4819_enable_rx_ctcss(&c62_bk4819, config->rxTone);
    }

    bk4819_rx_on(&c62_bk4819);
    radioStatus = RX;
}

void radio_enableTx()
{
    if (config->txDisable == 1 || radioMode == OPMODE_TETRA)
        return;

    gpioDev_clear(&c62_bk4819_gpio, GPIO_VHF_RX_LNA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_UHF_RX_LNA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_VHF_TX_PA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_UHF_TX_PA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_ALC_TX_LED);

    //Before enable TX path delay to allow hardware to settle
    delayMs(5);

    if (config->txFrequency < 136000000 || config->txFrequency > 600000000)
        return;

    bk4819_set_freq(&c62_bk4819, config->txFrequency);

    if (flatMode(radioMode))
        applyM17Fields(true);
    else if (config->txToneEn)
        bk4819_enable_tx_ctcss(&c62_bk4819, config->txTone);

    if (config->txFrequency < 174000000) {
        gpioDev_set(&c62_bk4819_gpio, GPIO_VHF_TX_PA);
    } else {
        gpioDev_set(&c62_bk4819_gpio, GPIO_UHF_TX_PA);
    }

    gpioDev_set(&c62_bk4819_gpio, GPIO_ALC_TX_LED);

    // depending on power level set PWM duty cycle for APC voltage control
    // Need table for this instead of quadratic mapping,
    // and also consider frequency dependence of PA efficiency
    // and battery voltage, but for now just use a crude quadratic mapping
    uint8_t power_percent =
        std::min(config->txPower * config->txPower * 100 / (5000 * 5000), 100U);
    if (power_percent > 60)
        power_percent = 60; // limit to 60% duty cycle for safety
    set_tx_power(power_percent);

    bk4819_tx_on(&c62_bk4819);
    radioStatus = TX;
}

void radio_disableRtx()
{
    bk4819_disable_ctdcss(&c62_bk4819);

    gpioDev_clear(&c62_bk4819_gpio, GPIO_VHF_RX_LNA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_UHF_RX_LNA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_VHF_TX_PA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_UHF_TX_PA);
    gpioDev_clear(&c62_bk4819_gpio, GPIO_ALC_TX_LED);

    bk4819_rtx_off(&c62_bk4819);
    radioStatus = OFF;
}

void radio_updateConfiguration()
{
    /*
     * Update VCO frequency and tuning parameters if current operating status
     * is different from OFF.
     * This is done by calling again the corresponding functions, which is safe
     * to do and avoids code duplication.
     */
    if (radioStatus == RX)
        radio_enableRx();
    if (radioStatus == TX)
        radio_enableTx();
}

rssi_t radio_getRssi()
{
#ifdef C62_REG43_SWEEP
    /* Called by the rtx thread in RX, so it can own the BK4819 here */
    if (flatMode(radioMode) && radioStatus == RX) {
        int64_t now = k_uptime_get();
        bool recording = true;
#ifdef C62_BASEBAND_DUMP
        recording = baseband_dump_recording();
#endif
        if (reg43SweepStarted && recording)
            reg43SweepElapsed += now - reg43SweepLast;
        reg43SweepLast = now;
        if (!reg43SweepStarted || reg43SweepElapsed >= C62_REG43_SWEEP_MS) {
            if (reg43SweepStarted)
                reg43SweepIndex = (reg43SweepIndex + 1)
                                % ARRAY_SIZE(reg43Sweep);
            reg43SweepStarted = true;
            reg43SweepElapsed = 0;
            c62_rx_reg43 = reg43Sweep[reg43SweepIndex];
            BK4819_writeReg(&c62_bk4819, BK4819_REG_43, c62_rx_reg43);
        }
    }
#endif
    return bk4819_get_rssi(&c62_bk4819);
}

enum opstatus radio_getStatus()
{
    return radioStatus;
}