/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <stddef.h>
#include "drivers/baseband/SI4732.h"
#include "interfaces/delays.h"

/* POWER_UP arguments */
#define POWER_UP_ARG1_XOSCEN 0x10
#define POWER_UP_ARG1_FUNC_FM 0x00
#define POWER_UP_ARG2_ANALOG_OUT 0x05

/* Interrupt acknowledge flag of the status commands */
#define STATUS_ARG1_INTACK 0x01

/* Maximum command execution times, with some margin */
#define POWER_UP_TIMEOUT_MS 300
#define CMD_TIMEOUT_MS 50
#define TUNE_TIMEOUT_MS 500

/* Settling time of the 32.768kHz crystal oscillator */
#define XOSC_SETTLE_MS 500

/* Minimum reset pulse is 100us, a longer one is harmless */
#define RESET_PULSE_MS 10
#define RESET_SETTLE_MS 10

#define FM_FREQ_UNIT_HZ 10000

/**
 * \internal
 * Poll the status byte until the chip is clear to send.
 *
 * @param dev: pointer to device data.
 * @param timeoutMs: maximum waiting time, in ms.
 * @param status: filled with the last status byte read.
 * @return zero on success, -EIO if the chip reports an error, -ETIMEDOUT on
 * timeout or the bus driver error.
 */
static int waitCts(const struct SI4732 *dev, uint32_t timeoutMs,
                   uint8_t *status)
{
    for (uint32_t elapsed = 0;; elapsed++) {
        int ret = i2c_read(dev->i2c, dev->addr, status, 1, true);
        if (ret < 0)
            return ret;

        if ((*status & SI4732_STATUS_CTS) != 0)
            return ((*status & SI4732_STATUS_ERR) != 0) ? -EIO : 0;

        if (elapsed >= timeoutMs)
            return -ETIMEDOUT;

        delayMs(1);
    }
}

/**
 * \internal
 * Send a command, wait for its completion and read its response.
 *
 * @param dev: pointer to device data.
 * @param cmd: command and arguments.
 * @param cmdLen: length of the command, in bytes.
 * @param resp: buffer for the response, status byte included, or NULL.
 * @param respLen: length of the response, in bytes.
 * @param timeoutMs: maximum command execution time, in ms.
 * @return zero on success, a negative error code otherwise.
 */
static int sendCommand(const struct SI4732 *dev, const uint8_t *cmd,
                       size_t cmdLen, uint8_t *resp, size_t respLen,
                       uint32_t timeoutMs)
{
    uint8_t status;

    i2c_acquire(dev->i2c);

    int ret = i2c_write(dev->i2c, dev->addr, cmd, cmdLen, true);
    if (ret == 0)
        ret = waitCts(dev, timeoutMs, &status);

    if ((ret == 0) && (resp != NULL) && (respLen > 0))
        ret = i2c_read(dev->i2c, dev->addr, resp, respLen, true);

    i2c_release(dev->i2c);

    return ret;
}

void si4732_reset(const struct SI4732 *dev)
{
    gpioPin_clear(&dev->rst);
    gpioPin_setMode(&dev->rst, OUTPUT);
    delayMs(RESET_PULSE_MS);
    gpioPin_set(&dev->rst);
    delayMs(RESET_SETTLE_MS);
}

int si4732_probe(const struct SI4732 *dev, uint8_t *status)
{
    uint8_t value = 0;

    si4732_reset(dev);

    i2c_acquire(dev->i2c);
    int ret = i2c_read(dev->i2c, dev->addr, &value, 1, true);
    i2c_release(dev->i2c);

    if (status != NULL)
        *status = value;

    return ret;
}

int si4732_fm_power_up(const struct SI4732 *dev, bool xosc)
{
    const uint8_t cmd[] = {
        SI4732_CMD_POWER_UP,
        POWER_UP_ARG1_FUNC_FM | (xosc ? POWER_UP_ARG1_XOSCEN : 0),
        POWER_UP_ARG2_ANALOG_OUT,
    };

    int ret = sendCommand(dev, cmd, sizeof(cmd), NULL, 0, POWER_UP_TIMEOUT_MS);
    if ((ret == 0) && xosc)
        delayMs(XOSC_SETTLE_MS);

    return ret;
}

int si4732_power_down(const struct SI4732 *dev)
{
    const uint8_t cmd[] = { SI4732_CMD_POWER_DOWN };

    return sendCommand(dev, cmd, sizeof(cmd), NULL, 0, CMD_TIMEOUT_MS);
}

int si4732_get_rev(const struct SI4732 *dev, struct si4732Rev *rev)
{
    const uint8_t cmd[] = { SI4732_CMD_GET_REV };
    uint8_t resp[9];

    int ret = sendCommand(dev, cmd, sizeof(cmd), resp, sizeof(resp),
                          CMD_TIMEOUT_MS);
    if (ret < 0)
        return ret;

    rev->partNumber = resp[1];
    rev->fwMajor = resp[2];
    rev->fwMinor = resp[3];
    rev->patchId = ((uint16_t)resp[4] << 8) | resp[5];
    rev->cmpMajor = resp[6];
    rev->cmpMinor = resp[7];
    rev->chipRev = resp[8];

    return 0;
}

int si4732_set_property(const struct SI4732 *dev, uint16_t prop, uint16_t value)
{
    const uint8_t cmd[] = {
        SI4732_CMD_SET_PROPERTY, 0x00,
        (uint8_t)(prop >> 8),    (uint8_t)(prop & 0xFF),
        (uint8_t)(value >> 8),   (uint8_t)(value & 0xFF),
    };

    return sendCommand(dev, cmd, sizeof(cmd), NULL, 0, CMD_TIMEOUT_MS);
}

int si4732_get_property(const struct SI4732 *dev, uint16_t prop,
                        uint16_t *value)
{
    const uint8_t cmd[] = {
        SI4732_CMD_GET_PROPERTY,
        0x00,
        (uint8_t)(prop >> 8),
        (uint8_t)(prop & 0xFF),
    };
    uint8_t resp[4];

    int ret = sendCommand(dev, cmd, sizeof(cmd), resp, sizeof(resp),
                          CMD_TIMEOUT_MS);
    if (ret == 0)
        *value = ((uint16_t)resp[2] << 8) | resp[3];

    return ret;
}

int si4732_fm_set_deemphasis(const struct SI4732 *dev, uint8_t us)
{
    uint16_t value;

    switch (us) {
        case 50:
            value = 1;
            break;
        case 75:
            value = 2;
            break;
        default:
            return -EINVAL;
    }

    return si4732_set_property(dev, SI4732_PROP_FM_DEEMPHASIS, value);
}

int si4732_set_volume(const struct SI4732 *dev, uint8_t volume)
{
    if (volume > 63)
        return -EINVAL;

    return si4732_set_property(dev, SI4732_PROP_RX_VOLUME, volume);
}

int si4732_set_mute(const struct SI4732 *dev, bool mute)
{
    // Bit 1 mutes the left channel, bit 0 the right one
    return si4732_set_property(dev, SI4732_PROP_RX_HARD_MUTE, mute ? 0x03 : 0);
}

int si4732_fm_tune(const struct SI4732 *dev, uint32_t freq,
                   struct si4732TuneStatus *status)
{
    if ((freq < SI4732_FM_FREQ_MIN) || (freq > SI4732_FM_FREQ_MAX))
        return -EINVAL;

    const uint16_t channel = (uint16_t)(freq / FM_FREQ_UNIT_HZ);
    const uint8_t tuneCmd[] = {
        SI4732_CMD_FM_TUNE_FREQ,
        0x00,
        (uint8_t)(channel >> 8),
        (uint8_t)(channel & 0xFF),
        0x00, // Automatic antenna tuning capacitor
    };

    int ret = sendCommand(dev, tuneCmd, sizeof(tuneCmd), NULL, 0,
                          CMD_TIMEOUT_MS);
    if (ret < 0)
        return ret;

    // Wait for the seek/tune complete flag
    const uint8_t intCmd[] = { SI4732_CMD_GET_INT_STATUS };
    uint8_t intStatus = 0;

    for (uint32_t elapsed = 0;; elapsed++) {
        ret = sendCommand(dev, intCmd, sizeof(intCmd), &intStatus, 1,
                          CMD_TIMEOUT_MS);
        if (ret < 0)
            return ret;

        if ((intStatus & SI4732_STATUS_STCINT) != 0)
            break;

        if (elapsed >= TUNE_TIMEOUT_MS)
            return -ETIMEDOUT;

        delayMs(1);
    }

    // Read the result, which also clears the seek/tune complete flag
    const uint8_t statusCmd[] = { SI4732_CMD_FM_TUNE_STATUS,
                                  STATUS_ARG1_INTACK };
    uint8_t resp[8];

    ret = sendCommand(dev, statusCmd, sizeof(statusCmd), resp, sizeof(resp),
                      CMD_TIMEOUT_MS);
    if ((ret < 0) || (status == NULL))
        return ret;

    status->valid = (resp[1] & 0x01) != 0;
    status->freq = (((uint32_t)resp[2] << 8) | resp[3]) * FM_FREQ_UNIT_HZ;
    status->rssi = resp[4];
    status->snr = resp[5];
    status->mult = resp[6];

    return 0;
}

int si4732_fm_get_rsq(const struct SI4732 *dev, struct si4732Rsq *rsq)
{
    const uint8_t cmd[] = { SI4732_CMD_FM_RSQ_STATUS, STATUS_ARG1_INTACK };
    uint8_t resp[8];

    int ret = sendCommand(dev, cmd, sizeof(cmd), resp, sizeof(resp),
                          CMD_TIMEOUT_MS);
    if (ret < 0)
        return ret;

    rsq->valid = (resp[2] & 0x01) != 0;
    rsq->softMuted = (resp[2] & 0x08) != 0;
    rsq->pilot = (resp[3] & 0x80) != 0;
    rsq->blend = resp[3] & 0x7F;
    rsq->rssi = resp[4];
    rsq->snr = resp[5];
    rsq->mult = resp[6];
    rsq->freqOff = (int8_t)resp[7];

    return 0;
}
