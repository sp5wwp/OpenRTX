/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SI4732_H
#define SI4732_H

#include <stdint.h>
#include <stdbool.h>
#include "peripherals/gpio.h"
#include "peripherals/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Driver for the Skyworks (Silicon Labs) SI4732 AM/FM/SW broadcast receiver,
 * controlled through the command interface of the Si47xx family (AN332). Only
 * the FM receiver with analog audio output is supported.
 *
 * Every command is written in a single I2C message, then the status byte is
 * polled until the chip is clear to send (CTS) and the response, if any, is
 * read back in a separate I2C message: the bus driver does not need to support
 * repeated starts or open transfers.
 */

/** 7-bit I2C address with the SEN pin tied low. */
#define SI4732_I2C_ADDR_SEN_LOW 0x11
/** 7-bit I2C address with the SEN pin tied high. */
#define SI4732_I2C_ADDR_SEN_HIGH 0x63

/** FM tuning range, in Hz. */
#define SI4732_FM_FREQ_MIN 64000000UL
#define SI4732_FM_FREQ_MAX 108000000UL

/* Commands */
#define SI4732_CMD_POWER_UP 0x01
#define SI4732_CMD_GET_REV 0x10
#define SI4732_CMD_POWER_DOWN 0x11
#define SI4732_CMD_SET_PROPERTY 0x12
#define SI4732_CMD_GET_PROPERTY 0x13
#define SI4732_CMD_GET_INT_STATUS 0x14
#define SI4732_CMD_FM_TUNE_FREQ 0x20
#define SI4732_CMD_FM_TUNE_STATUS 0x22
#define SI4732_CMD_FM_RSQ_STATUS 0x23

/* Status byte */
#define SI4732_STATUS_CTS 0x80
#define SI4732_STATUS_ERR 0x40
#define SI4732_STATUS_RSQINT 0x08
#define SI4732_STATUS_RDSINT 0x04
#define SI4732_STATUS_ASQINT 0x02
#define SI4732_STATUS_STCINT 0x01

/* Properties */
#define SI4732_PROP_FM_DEEMPHASIS 0x1100
#define SI4732_PROP_RX_VOLUME 0x4000
#define SI4732_PROP_RX_HARD_MUTE 0x4001

/**
 * SI4732 device data.
 */
struct SI4732 {
    const struct i2cDevice *i2c; ///< I2C bus device
    struct gpioPin rst;          ///< Reset, active low
    uint8_t addr;                ///< 7-bit I2C address, see SEN pin
};

/**
 * Chip revision, as reported by the GET_REV command.
 */
struct si4732Rev {
    uint8_t partNumber; ///< Last two digits of the part number (32: SI4732)
    uint8_t fwMajor;    ///< Firmware major revision, ASCII
    uint8_t fwMinor;    ///< Firmware minor revision, ASCII
    uint16_t patchId;   ///< Patch ID
    uint8_t cmpMajor;   ///< Component major revision, ASCII
    uint8_t cmpMinor;   ///< Component minor revision, ASCII
    uint8_t chipRev;    ///< Chip revision, ASCII
};

/**
 * Result of a FM tuning.
 */
struct si4732TuneStatus {
    uint32_t freq; ///< Tuned frequency, in Hz
    uint8_t rssi;  ///< Received signal strength, in dBuV
    uint8_t snr;   ///< Signal to noise ratio, in dB
    uint8_t mult;  ///< Multipath indicator, 0 to 100
    bool valid;    ///< The channel is considered valid
};

/**
 * FM received signal quality.
 */
struct si4732Rsq {
    uint8_t rssi;   ///< Received signal strength, in dBuV
    uint8_t snr;    ///< Signal to noise ratio, in dB
    uint8_t mult;   ///< Multipath indicator, 0 to 100
    int8_t freqOff; ///< Frequency offset, in kHz
    uint8_t blend;  ///< Stereo blend, 0 (mono) to 100 (full stereo), in %
    bool pilot;     ///< A stereo pilot is received
    bool valid;     ///< The channel is considered valid
    bool softMuted; ///< The audio is soft muted
};

/**
 * Hardware reset the chip, leaving it in the powered down state.
 *
 * Configures the reset pin as output.
 *
 * @param dev: pointer to device data.
 */
void si4732_reset(const struct SI4732 *dev);

/**
 * Reset the chip and check that it answers on the I2C bus.
 *
 * @param dev: pointer to device data.
 * @param status: if not NULL, filled with the status byte read.
 * @return zero if the chip answers, a negative error code otherwise.
 */
int si4732_probe(const struct SI4732 *dev, uint8_t *status);

/**
 * Power up the FM receiver, with analog audio output.
 *
 * When the 32.768kHz crystal oscillator is used, this function waits for it to
 * settle, which takes about 500ms.
 *
 * @param dev: pointer to device data.
 * @param xosc: true to enable the crystal oscillator, false when an external
 * reference clock is provided on the RCLK pin.
 * @return zero on success, a negative error code otherwise.
 */
int si4732_fm_power_up(const struct SI4732 *dev, bool xosc);

/**
 * Power down the chip.
 *
 * @param dev: pointer to device data.
 * @return zero on success, a negative error code otherwise.
 */
int si4732_power_down(const struct SI4732 *dev);

/**
 * Read the chip revision. The chip must be powered up.
 *
 * @param dev: pointer to device data.
 * @param rev: filled with the revision information.
 * @return zero on success, a negative error code otherwise.
 */
int si4732_get_rev(const struct SI4732 *dev, struct si4732Rev *rev);

/**
 * Set a property.
 *
 * @param dev: pointer to device data.
 * @param prop: property identifier.
 * @param value: property value.
 * @return zero on success, a negative error code otherwise.
 */
int si4732_set_property(const struct SI4732 *dev, uint16_t prop,
                        uint16_t value);

/**
 * Get a property.
 *
 * @param dev: pointer to device data.
 * @param prop: property identifier.
 * @param value: filled with the property value.
 * @return zero on success, a negative error code otherwise.
 */
int si4732_get_property(const struct SI4732 *dev, uint16_t prop,
                        uint16_t *value);

/**
 * Set the FM de-emphasis time constant.
 *
 * @param dev: pointer to device data.
 * @param us: time constant, 50 or 75us.
 * @return zero on success, -EINVAL for an unsupported time constant, another
 * negative error code otherwise.
 */
int si4732_fm_set_deemphasis(const struct SI4732 *dev, uint8_t us);

/**
 * Set the audio output volume.
 *
 * @param dev: pointer to device data.
 * @param volume: volume, 0 (muted) to 63 (maximum).
 * @return zero on success, a negative error code otherwise.
 */
int si4732_set_volume(const struct SI4732 *dev, uint8_t volume);

/**
 * Mute or unmute both audio output channels.
 *
 * @param dev: pointer to device data.
 * @param mute: true to mute.
 * @return zero on success, a negative error code otherwise.
 */
int si4732_set_mute(const struct SI4732 *dev, bool mute);

/**
 * Tune the FM receiver and wait for the tuning to complete.
 *
 * @param dev: pointer to device data.
 * @param freq: frequency, in Hz, with a 10kHz resolution.
 * @param status: if not NULL, filled with the tuning result.
 * @return zero on success, -EINVAL if the frequency is out of the FM range,
 * another negative error code otherwise.
 */
int si4732_fm_tune(const struct SI4732 *dev, uint32_t freq,
                   struct si4732TuneStatus *status);

/**
 * Read the FM received signal quality.
 *
 * @param dev: pointer to device data.
 * @param rsq: filled with the signal quality.
 * @return zero on success, a negative error code otherwise.
 */
int si4732_fm_get_rsq(const struct SI4732 *dev, struct si4732Rsq *rsq);

#ifdef __cplusplus
}
#endif

#endif /* SI4732_H */
