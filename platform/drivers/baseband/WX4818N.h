/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef WX4818N_H
#define WX4818N_H

#include <stdint.h>
#include <stdbool.h>
#include "peripherals/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Driver for the WX4818N FM transceiver, as used in the AnyTone AT-D890UV.
 * No datasheet is available: register usage is taken from the stock AnyTone
 * D890UV firmware V1.05 (D890UV_V1.05_20260521.CDD).
 *
 * The chip has a 3-wire interface: 7-bit register address with R/W flag, then
 * 16-bit data, MSB first, data latched on the rising clock edge.
 */

/**
 * WX4818N device: bus lines and chip select. Several devices can share the
 * clock and data lines.
 */
struct wx4818n {
    struct gpioPin clk; ///< Serial clock
    struct gpioPin dio; ///< Bidirectional serial data
    struct gpioPin cs;  ///< Chip select, active low
};

/**
 * Configure the bus lines and initialise the chip with the stock power-up
 * register sequence.
 *
 * @param dev: device.
 */
void wx4818n_init(const struct wx4818n *dev);

/**
 * Write a register.
 *
 * @param dev: device.
 * @param reg: register address.
 * @param value: register value.
 */
void wx4818n_writeReg(const struct wx4818n *dev, uint8_t reg, uint16_t value);

/**
 * Read a register.
 *
 * @param dev: device.
 * @param reg: register address.
 * @return register value.
 */
uint16_t wx4818n_readReg(const struct wx4818n *dev, uint8_t reg);

/**
 * Set the level of one of the chip GPIO outputs, GPIO0 to GPIO7.
 *
 * @param dev: device.
 * @param pin: GPIO number.
 * @param level: output level.
 */
void wx4818n_setGpio(const struct wx4818n *dev, uint8_t pin, bool level);

/**
 * Set the output of the chip DAC (GPON pin). The chip must be receiving:
 * the new value is applied by returning to receive mode.
 *
 * @param dev: device.
 * @param value: 8-bit DAC value.
 */
void wx4818n_setDac(const struct wx4818n *dev, uint8_t value);

/**
 * Configure the chip for FM reception on a given frequency and start
 * receiving.
 *
 * @param dev: device.
 * @param freq: frequency, in Hz.
 * @param wide: 25kHz channel (12.5kHz otherwise).
 */
void wx4818n_startFmRx(const struct wx4818n *dev, uint32_t freq, bool wide);

/**
 * Tune the synthesizer.
 *
 * @param dev: device.
 * @param freq: frequency, in Hz.
 */
void wx4818n_setFrequency(const struct wx4818n *dev, uint32_t freq);

/**
 * FM transmission parameters.
 */
struct wx4818nTxConfig {
    uint32_t freq;    ///< Frequency in Hz, already corrected for the crystal
    bool wide;        ///< 25kHz channel (12.5kHz otherwise)
    uint16_t ctcss;   ///< CTCSS tone in 0.1Hz, 0 for none
    uint16_t modCal;  ///< Modulation calibration, register 0x31 bits 11:0
    uint16_t toneCal; ///< Modulation calibration, register 0x2E entry 6
    bool baseband;    ///< Flat modulator for a digital baseband (4FSK)
};

/**
 * Configure the chip for FM transmission and switch it to TX mode, with the
 * sequence of the stock firmware. The RF power stage is not handled here.
 *
 * @param dev: device.
 * @param cfg: transmission parameters.
 */
void wx4818n_startFmTx(const struct wx4818n *dev,
                       const struct wx4818nTxConfig *cfg);

/**
 * Put the chip in standby.
 *
 * @param dev: device.
 */
void wx4818n_standby(const struct wx4818n *dev);

#ifdef __cplusplus
}
#endif

#endif // WX4818N_H
