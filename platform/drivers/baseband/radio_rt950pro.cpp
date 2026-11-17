/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <hwconfig.h>
#include <interfaces/radio.h>
#include <interfaces/delays.h>
#include "radioUtils.h"

/*
 * Reception-only radio driver for the Radtel RT-950 Pro.
 *
 * The radio has two BK4829 transceivers, the first one wired to the VHF LNA
 * and PA and the second to the UHF ones. Only the receive RF path switches
 * are controlled. Transmission is not supported yet.
 */

static const rtxStatus_t *config;   // Pointer to the radio configuration
static enum opstatus radioStatus;   // Current operating status
static const struct BK4829 *rxChip; // Chip currently receiving, or NULL

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

    rfPathIdle();

    gpioPin_setMode(&rt950_rf_u6r, OUTPUT); // HF RX switch
    gpioPin_setMode(&rt950_rf_u3r, OUTPUT); // UHF RX switch
    gpioPin_setMode(&rt950_rf_v3r, OUTPUT); // VHF RX switch

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
    (void)mode;
}

bool radio_checkRxDigitalSquelch()
{
    if (rxChip == NULL)
        return false;

    return bk4829_get_ctcss(rxChip);
}

void radio_enableAfOutput()
{
    if (rxChip != NULL)
        bk4829_set_af(rxChip, BK4829_AF_TYPE_FM);
}

void radio_disableAfOutput()
{
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
}

void radio_enableTx()
{
    // Transmission is not supported yet
}

void radio_disableRtx()
{
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
