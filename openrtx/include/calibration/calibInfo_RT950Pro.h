/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CALIBINFO_RT950PRO_H
#define CALIBINFO_RT950PRO_H

#include <stdint.h>

/** Transmit power levels of the factory calibration */
enum rt950proPowerLevel {
    RT950PRO_POWER_HIGH = 0,
    RT950PRO_POWER_MID = 1,
    RT950PRO_POWER_LOW = 2,
};

/** Band rows of the factory calibration */
enum rt950proCalRow {
    RT950PRO_CAL_UHF = 0,    ///< 400 MHz and up, 10 MHz per point
    RT950PRO_CAL_VHF = 1,    ///< 130-199 MHz, 3 MHz per point
    RT950PRO_CAL_200MHZ = 2, ///< 200-299 MHz, 5 MHz per point
    RT950PRO_CAL_350MHZ = 3, ///< 350-399 MHz, 5 MHz per point
};

#define RT950PRO_CAL_LEVELS 3
#define RT950PRO_CAL_ROWS 4
#define RT950PRO_CAL_POINTS 16

/**
 * Factory calibration of the Radtel RT-950 Pro, as the vendor firmware keeps
 * it in the external SPI NOR from address 0xF000 (vendor firmware V0.27,
 * loader at 0x08011DC8, lookup at 0x0801E2E0; see docs/TX_SEQUENCE.md of the
 * reverse engineering notes). Bytes of 0x00 or 0xFF are not programmed.
 */
struct rt950proCalib {
    /// PA drive (DAC2, 8 bit) per power level, band row and frequency point
    uint8_t txPower[RT950PRO_CAL_LEVELS][RT950PRO_CAL_ROWS][RT950PRO_CAL_POINTS];
    uint8_t unknownC0[0x20]; ///< 0xF0C0, not used
    uint8_t blank;           ///< 0xF0E0: 0xFF if never calibrated
};

/** Address of the calibration in the external SPI NOR */
#define RT950PRO_CAL_NOR_ADDR 0xF000

#endif // CALIBINFO_RT950PRO_H
