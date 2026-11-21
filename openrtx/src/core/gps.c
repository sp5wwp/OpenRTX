/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "interfaces/platform.h"
#include "core/gps.h"
#include <minmea.h>
#include <stdio.h>
#include "core/state.h"
#include <string.h>
#include <stdbool.h>
#include "core/utils.h"

#define KNOTS2KMH(x) ((((int) x) * 1852) / 1000)

/*
 * Multi-constellation receivers report the satellites of each constellation
 * in their own GSV sequence and the satellites used in the fix in their own
 * GSA sentence, told apart by the talker ID ($GPGSV, $BDGSV, ...). Each
 * constellation is assembled on its own, then the strongest satellites of all
 * of them fill the state.
 */
#define GPS_SATS                 12   // Satellites in the state, and per GSV sequence
#define GPS_STALE_CYCLES         3    // Fix cycles (GGA) a constellation may go unreported

struct constellation
{
    gpssat_t sats[GPS_SATS];          ///< Satellites of the last complete GSV sequence
    gpssat_t pending[GPS_SATS];       ///< GSV sequence being received
    uint8_t  used[GPS_SATS];          ///< IDs used in the fix, last GSA
    uint8_t  numSats;                 ///< Entries in sats
    uint8_t  numPending;              ///< Entries in pending
    uint8_t  numUsed;                 ///< Entries in used
    uint8_t  inView;                  ///< Satellites in view, last GSV sequence
    uint8_t  age;                     ///< Fix cycles since the last GSV sequence
};

/* Talker IDs of the constellations, index of their entry */
static const char talkers[][3] = { "GP", "GL", "GA", "BD", "GQ" };
#define GPS_GPS     0
#define GPS_GLONASS 1
#define GPS_BEIDOU  3

static struct constellation constellations[ARRAY_SIZE(talkers)];
static bool gpsEnabled = false;

/**
 * Constellation of a sentence from its talker ID, -1 for the combined talker
 * ($GN) or an unknown one.
 */
static int constellationOf(const char *sentence)
{
    const char *talker = &sentence[1];

    // BeiDou has two talker IDs, depending on the NMEA version
    if((talker[0] == 'G') && (talker[1] == 'B'))
        return GPS_BEIDOU;

    for(size_t i = 0; i < ARRAY_SIZE(talkers); i++)
    {
        if((talker[0] == talkers[i][0]) && (talker[1] == talkers[i][1]))
            return (int) i;
    }

    return -1;
}

static void parseGsv(const char *sentence)
{
    struct minmea_sentence_gsv frame;
    int c = constellationOf(sentence);

    if((c < 0) || (minmea_parse_gsv(&frame, sentence) == false))
        return;

    struct constellation *con = &constellations[c];
    if(frame.msg_nr == 1)
        con->numPending = 0;

    for(int i = 0; i < 4; i++)
    {
        if((frame.sats[i].nr == 0) || (con->numPending >= GPS_SATS))
            continue;

        gpssat_t *sat = &con->pending[con->numPending++];
        sat->id        = frame.sats[i].nr;
        sat->elevation = frame.sats[i].elevation;
        sat->azimuth   = frame.sats[i].azimuth;
        sat->snr       = frame.sats[i].snr;
    }

    // Last sentence of the sequence: it replaces the previous one
    if(frame.msg_nr == frame.total_msgs)
    {
        memcpy(con->sats, con->pending, sizeof(con->sats));
        con->numSats = con->numPending;
        con->inView  = frame.total_sats;
        con->age     = 0;
    }
}

static void parseGsa(const char *sentence, gps_t *data)
{
    struct minmea_sentence_gsa frame;
    int c = constellationOf(sentence);

    if(minmea_parse_gsa(&frame, sentence) == false)
        return;

    data->hdop     = minmea_toscaledint(&frame.hdop, 100);
    data->fix_type = frame.fix_type;

    // A combined $GNGSA (NMEA 2.3) tells the constellations apart only by
    // the satellite numbering: GPS 1 to 32, GLONASS 65 to 96
    if(c < 0)
    {
        if(frame.sats[0] == 0)
            return;
        c = (frame.sats[0] <= 32) ? GPS_GPS : GPS_GLONASS;
    }

    struct constellation *con = &constellations[c];
    con->numUsed = 0;
    for(int i = 0; i < GPS_SATS; i++)
    {
        if(frame.sats[i] != 0)
            con->used[con->numUsed++] = frame.sats[i];
    }
}

/* A fix cycle went by: forget the constellations no longer reported */
static void ageConstellations()
{
    for(size_t c = 0; c < ARRAY_SIZE(constellations); c++)
    {
        struct constellation *con = &constellations[c];
        if(con->age < GPS_STALE_CYCLES)
        {
            con->age++;
            continue;
        }

        con->numSats = 0;
        con->numUsed = 0;
        con->inView  = 0;
    }
}

static bool isUsed(const struct constellation *con, uint8_t id)
{
    for(uint8_t i = 0; i < con->numUsed; i++)
    {
        if(con->used[i] == id)
            return true;
    }

    return false;
}

/* The strongest satellites of all the constellations, into the state */
static void mergeConstellations(gps_t *data)
{
    bool    active[GPS_SATS] = { false };
    uint8_t count = 0;

    memset(data->satellites, 0x00, sizeof(data->satellites));
    data->satellites_in_view = 0;

    for(size_t c = 0; c < ARRAY_SIZE(constellations); c++)
    {
        const struct constellation *con = &constellations[c];
        data->satellites_in_view += con->inView;

        for(uint8_t s = 0; s < con->numSats; s++)
        {
            const gpssat_t *sat = &con->sats[s];

            // Insertion by decreasing SNR, the weakest falls off the end
            int pos = count;
            while((pos > 0) && (data->satellites[pos - 1].snr < sat->snr))
                pos--;
            if(pos >= GPS_SATS)
                continue;

            int last = (count < GPS_SATS) ? count : (GPS_SATS - 1);
            for(int i = last; i > pos; i--)
            {
                data->satellites[i] = data->satellites[i - 1];
                active[i] = active[i - 1];
            }

            data->satellites[pos] = *sat;
            active[pos] = isUsed(con, sat->id);
            if(count < GPS_SATS)
                count++;
        }
    }

    data->active_sats = 0;
    for(int i = 0; i < GPS_SATS; i++)
    {
        if(active[i])
            data->active_sats |= 1u << i;
    }
}

#ifdef CONFIG_RTC
static bool rtcSyncDone = false;
static void syncRtc(datetime_t timestamp)
{
    if(state.settings.gpsSetTime == false) {
        rtcSyncDone = false;
        return;
    }

    if(rtcSyncDone)
        return;

    platform_setTime(timestamp);
    rtcSyncDone = true;
}
#endif

void gps_task(const struct gpsDevice *dev)
{
    char sentence[2*MINMEA_MAX_LENGTH];
    int ret;

    // No GPS, return
    if(dev == NULL)
        return;

    // Handle GPS turn on/off
    if(state.settings.gps_enabled != gpsEnabled)
    {
        gpsEnabled = state.settings.gps_enabled;

        if(gpsEnabled)
            gps_enable(dev);
        else
            gps_disable(dev);
    }

    // GPS disabled, nothing to do
    if(gpsEnabled == false)
        return;

    // Acquire a new NMEA sentence from GPS
    ret = gps_getSentence(dev, sentence, sizeof(sentence));
    if(ret <= 0)
        return;

    // Parse the sentence. Work on a local state copy to minimize the time
    // spent with the state mutex locked
    gps_t gps_data;
    pthread_mutex_lock(&state_mutex);
    gps_data = state.gps_data;
    pthread_mutex_unlock(&state_mutex);

    int32_t sId = minmea_sentence_id(sentence, false);
    switch(sId)
    {
        case MINMEA_SENTENCE_RMC:
        {
            struct minmea_sentence_rmc frame;
            if (minmea_parse_rmc(&frame, sentence))
            {
                gps_data.timestamp.hour = frame.time.hours;
                gps_data.timestamp.minute = frame.time.minutes;
                gps_data.timestamp.second = frame.time.seconds;
                gps_data.timestamp.day = 0;
                gps_data.timestamp.date = frame.date.day;
                gps_data.timestamp.month = frame.date.month;
                gps_data.timestamp.year = frame.date.year;
                gps_data.speed = KNOTS2KMH(minmea_toint(&frame.speed));
                #ifdef CONFIG_RTC
                if(frame.valid)
                    syncRtc(gps_data.timestamp);
                #endif
            }
        }
        break;

        case MINMEA_SENTENCE_GGA:
        {
            struct minmea_sentence_gga frame;
            if (minmea_parse_gga(&frame, sentence))
            {
                gps_data.latitude = minmea_tofixedpoint(&frame.latitude);
                gps_data.longitude = minmea_tofixedpoint(&frame.longitude);
                gps_data.fix_quality = frame.fix_quality;
                gps_data.satellites_tracked = frame.satellites_tracked;
                gps_data.altitude = minmea_toint(&frame.altitude);
            }

            // GGA comes once per fix cycle
            ageConstellations();
            mergeConstellations(&gps_data);
        }
        break;

        case MINMEA_SENTENCE_GSA:
            parseGsa(sentence, &gps_data);
            mergeConstellations(&gps_data);
            break;

        case MINMEA_SENTENCE_GSV:
            parseGsv(sentence);
            mergeConstellations(&gps_data);
            break;

        case MINMEA_SENTENCE_VTG:
        {
            struct minmea_sentence_vtg frame;
            if (minmea_parse_vtg(&frame, sentence))
            {
                gps_data.speed = minmea_toint(&frame.speed_kph);
                gps_data.tmg_mag = minmea_toint(&frame.magnetic_track_degrees);
                gps_data.tmg_true = minmea_toint(&frame.true_track_degrees);
            }
        }
        break;

        // Ignore this message as we take data from RMC
        case MINMEA_SENTENCE_GLL: break;

        // These messages are never sent by the Jumpstar JS-M710 Module
        case MINMEA_SENTENCE_GST: break;
        case MINMEA_SENTENCE_ZDA: break;

        // Error handling
        case MINMEA_INVALID: break;
        case MINMEA_UNKNOWN: break;
    }

    // Update GPS data inside radio state
    pthread_mutex_lock(&state_mutex);
    state.gps_data = gps_data;
    pthread_mutex_unlock(&state_mutex);
}
