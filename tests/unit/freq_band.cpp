/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>

extern "C" {
#include "core/utils.h"
}

#ifdef CONFIG_RF_BANDS_MHZ

/* Built with the bands 28-30, 136-174 and 400-480 MHz. */
TEST_CASE("A frequency is in the band of the list that holds it", "[band]")
{
    REQUIRE(freqBand(29000000) == 0);
    REQUIRE(freqBand(145500000) == 1);
    REQUIRE(freqBand(433000000) == 2);
}

TEST_CASE("The edges of a listed band are part of it", "[band]")
{
    REQUIRE(freqBand(28000000) == 0);
    REQUIRE(freqBand(30000000) == 0);
    REQUIRE(freqBand(400000000) == 2);
    REQUIRE(freqBand(480000000) == 2);
    REQUIRE(freqInBand(479975000));
}

TEST_CASE("A frequency outside the listed bands has none", "[band]")
{
    REQUIRE(freqBand(27999999) == -1);
    REQUIRE(freqBand(30000001) == -1);
    REQUIRE(freqBand(222000000) == -1);
    REQUIRE(freqBand(480000001) == -1);
    REQUIRE_FALSE(freqInBand(222000000));
}

#else

/* The emulator's hardware info: VHF 136-174 MHz, UHF 400-480 MHz. */
TEST_CASE("Without a list the bands are those of the hardware info", "[band]")
{
    REQUIRE(freqBand(145500000) == 0);
    REQUIRE(freqBand(136000000) == 0);
    REQUIRE(freqBand(174000000) == 0);
    REQUIRE(freqBand(433000000) == 1);
    REQUIRE(freqBand(400000000) == 1);
    REQUIRE(freqBand(480000000) == 1);
    REQUIRE(freqInBand(479975000));
}

TEST_CASE("A frequency outside the hardware info's limits has no band",
          "[band]")
{
    REQUIRE(freqBand(135999999) == -1);
    REQUIRE(freqBand(174000001) == -1);
    REQUIRE(freqBand(29000000) == -1);
    REQUIRE(freqBand(480000001) == -1);
    REQUIRE_FALSE(freqInBand(222000000));
}

#endif
