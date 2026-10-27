/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_TETRA_STATUS_H
#define OPENRTX_TETRA_STATUS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What the TETRA receive mode has decoded, for the UI. Written by the rtx
 * thread, read with tetra_getStatus().
 */
struct tetraStatus {
    bool sync;          /**< Locked to a TMO downlink */
    bool cell;          /**< Fields below up to dlFreq are valid */
    uint16_t mcc;       /**< Mobile country code */
    uint16_t mnc;       /**< Mobile network code */
    uint8_t colourCode; /**< Colour code */
    bool sysinfo;       /**< la and dlFreq are valid */
    uint16_t la;        /**< Location area */
    uint32_t dlFreq;    /**< Main carrier downlink frequency, Hz */
    bool airEncryption; /**< Cell uses air interface encryption */

    bool call;          /**< A call is set up or traffic is flowing */
    bool callGroup;     /**< Group call */
    bool callEncrypted; /**< Call or its traffic is encrypted */
    bool voice;         /**< Traffic seen in the last second */
    uint16_t callId;    /**< Call identifier */
    uint32_t callDst;   /**< Called (group) SSI, 0 if unknown */
    uint32_t talker;    /**< Talking party SSI, 0 if unknown */

    uint32_t sdsSrc;    /**< Sender of the last SDS, 0 if none */
    char sds[64];       /**< Last SDS text or status, ASCII */
    long long sdsTime;  /**< Tick of the last SDS */

    uint16_t phaseErr;  /**< Demodulator RMS phase error, % of pi/4 */
    uint16_t overruns;  /**< Sample blocks lost: the radio was too slow */
    uint16_t resyncs;   /**< Burst synchronisation restarts */
    int16_t carrierHz;  /**< Carrier offset estimate */
};

/**
 * Copy the current TETRA status.
 *
 * @param out: destination.
 */
void tetra_getStatus(struct tetraStatus *out);

/* For the TETRA mode */
struct tetra_event;

/**
 * Clear the status (new frequency, mode change).
 */
void tetraStatus_reset(void);

/**
 * libtetra event callback: updates the status. Pass it in the libtetra
 * receiver configuration.
 */
void tetraStatus_onEvent(void *user, const struct tetra_event *ev);

/**
 * Mark the burst synchronisation as lost, keeping the cell information.
 *
 * @param overrun: because samples were lost.
 */
void tetraStatus_lostSync(bool overrun);

/**
 * Update the demodulator figures.
 */
void tetraStatus_setDemod(unsigned phaseErr, int carrierHz);

/**
 * @return true while locked to a TMO downlink.
 */
bool tetraStatus_sync(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_TETRA_STATUS_H */
