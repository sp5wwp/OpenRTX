/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "core/gps.h"
#include "core/state.h"
}

/*
 * Sentences of a GPS + BeiDou receiver (ATGM336H, as on the Radtel RT-950
 * Pro): one GSV sequence and one GSA sentence per constellation, told apart
 * by the talker ID.
 */

static std::vector<std::string> queue;

static void fakeEnable(void *priv)
{
    (void)priv;
}

static void fakeDisable(void *priv)
{
    (void)priv;
}

static int fakeGetSentence(void *priv, char *buf, const size_t bufSize)
{
    (void)priv;

    if (queue.empty())
        return 0;

    std::string s = queue.front();
    queue.erase(queue.begin());
    if (s.size() >= bufSize)
        return -1;

    memcpy(buf, s.c_str(), s.size() + 1);
    return (int)s.size();
}

static const struct gpsDevice fakeGps = {
    .priv = NULL,
    .enable = fakeEnable,
    .disable = fakeDisable,
    .getSentence = fakeGetSentence,
};

/* Sentence with its checksum and line ending */
static std::string nmea(const std::string &body)
{
    uint8_t sum = 0;
    for (char c : body)
        sum ^= (uint8_t)c;

    char tail[8];
    snprintf(tail, sizeof(tail), "*%02X\r\n", sum);
    return "$" + body + tail;
}

static void feed(const std::vector<std::string> &bodies)
{
    for (const auto &b : bodies)
        queue.push_back(nmea(b));

    while (!queue.empty())
        gps_task(&fakeGps);
}

static const char *GGA = "GNGGA,120000.000,5213.0000,N,02100.0000,E,1,08,1.2,"
                         "100.0,M,0.0,M,,";

/* Position in the state of a satellite, -1 if absent */
static int find(const gps_t &gps, uint8_t id, uint8_t snr)
{
    for (int i = 0; i < 12; i++) {
        if ((gps.satellites[i].id == id) && (gps.satellites[i].snr == snr))
            return i;
    }

    return -1;
}

TEST_CASE("GPS satellites of several constellations", "[gps]")
{
    state_init();
    state.settings.gps_enabled = true;

    SECTION("GPS and BeiDou merged by SNR, fix flags per constellation")
    {
        feed({
            "GPGSV,2,1,06,01,10,010,10,02,20,020,20,03,30,030,30,04,40,040,40",
            "GPGSV,2,2,06,05,50,050,50,06,60,060,60",
            "BDGSV,1,1,03,01,15,100,45,05,25,200,15,20,35,300,35",
            "GPGSA,A,3,02,04,,,,,,,,,,,2.0,1.2,1.6",
            "BDGSA,A,3,20,,,,,,,,,,,,2.0,1.2,1.6",
            GGA,
        });

        const gps_t &gps = state.gps_data;
        REQUIRE(gps.satellites_in_view == 9);

        // Decreasing SNR across both constellations
        for (int i = 1; i < 9; i++)
            REQUIRE(gps.satellites[i - 1].snr >= gps.satellites[i].snr);
        REQUIRE(gps.satellites[0].snr == 60);
        REQUIRE(gps.satellites[9].id == 0);

        // GPS 2 and 4 and BeiDou 20 are used; BeiDou 1 and 5 are not, even
        // though GPS uses no satellite 1 or 5 either
        int gps2 = find(gps, 2, 20), gps4 = find(gps, 4, 40);
        int bd20 = find(gps, 20, 35), bd1 = find(gps, 1, 45);
        int bd5 = find(gps, 5, 15), gps5 = find(gps, 5, 50);
        REQUIRE(gps2 >= 0);
        REQUIRE(gps4 >= 0);
        REQUIRE(bd20 >= 0);
        REQUIRE(bd1 >= 0);
        REQUIRE((gps.active_sats & (1u << gps2)) != 0);
        REQUIRE((gps.active_sats & (1u << gps4)) != 0);
        REQUIRE((gps.active_sats & (1u << bd20)) != 0);
        REQUIRE((gps.active_sats & (1u << bd1)) == 0);
        REQUIRE((gps.active_sats & (1u << bd5)) == 0);
        REQUIRE((gps.active_sats & (1u << gps5)) == 0);

        // A new BeiDou sequence does not erase the GPS satellites
        feed({ "BDGSV,1,1,01,07,15,100,33" });
        REQUIRE(find(state.gps_data, 6, 60) >= 0);
        REQUIRE(find(state.gps_data, 7, 33) >= 0);
        REQUIRE(find(state.gps_data, 1, 45) == -1);
        REQUIRE(state.gps_data.satellites_in_view == 7);
    }

    SECTION("Third GSV sentence and the twelve strongest satellites")
    {
        feed({
            "GPGSV,3,1,10,01,10,010,11,02,20,020,12,03,30,030,13,04,40,040,14",
            "GPGSV,3,2,10,05,50,050,15,06,60,060,16,07,10,070,17,08,20,080,18",
            "GPGSV,3,3,10,09,30,090,19,10,40,100,20",
            "BDGSV,1,1,04,31,10,010,41,32,20,020,42,33,30,030,43,34,40,040,44",
            GGA,
        });

        const gps_t &gps = state.gps_data;
        REQUIRE(gps.satellites_in_view == 14);

        // Satellites 9 and 10 come from the third sentence
        REQUIRE(find(gps, 9, 19) >= 0);
        REQUIRE(find(gps, 10, 20) >= 0);

        // 14 in view, the two weakest do not fit
        REQUIRE(gps.satellites[0].id == 34);
        REQUIRE(gps.satellites[11].snr == 13);
        REQUIRE(find(gps, 1, 11) == -1);
        REQUIRE(find(gps, 2, 12) == -1);
    }

    SECTION("A constellation no longer reported is dropped")
    {
        feed({
            "GPGSV,1,1,01,01,10,010,30",
            "BDGSV,1,1,01,11,10,010,40",
            GGA,
        });
        REQUIRE(state.gps_data.satellites_in_view == 2);

        // BeiDou goes silent for a few fix cycles
        for (int i = 0; i < 4; i++)
            feed({ "GPGSV,1,1,01,01,10,010,30", GGA });

        REQUIRE(state.gps_data.satellites_in_view == 1);
        REQUIRE(find(state.gps_data, 11, 40) == -1);
        REQUIRE(find(state.gps_data, 1, 30) == 0);
    }
}
