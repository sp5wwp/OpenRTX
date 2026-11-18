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

/*
 * Receiver diagnostics, for test and remote control. Read without locking:
 * each field is consistent, the set is approximately so.
 */
struct tetraDiag {
    uint32_t crcOk;      /**< Downlink blocks that passed the CRC, total */
    uint32_t realigned;  /**< libtetra slot grid moves */
    bool locked;         /**< libtetra lock */
    uint32_t reacquired; /**< Demodulator re-acquisitions */
    uint32_t slips;      /**< Demodulator timing jumps followed */
    uint32_t symbols;    /**< Symbols since the last (re)acquisition */
    unsigned phaseErr;   /**< RMS phase error, % of pi/4 */
    int carrierHz;       /**< Carrier offset */
    int clockPpm;        /**< Sample clock offset */
    bool eqSeed;         /**< Equalizer started from the measured taps */
    float eq[9];         /**< Equalizer taps */
};

/**
 * Copy the receiver diagnostics (TETRA mode only, defined by the opmode).
 */
void tetra_getDiag(struct tetraDiag *out);

/**
 * Restart the receiver from scratch at its next update.
 *
 * @param seedEqualizer: start the equalizer from the measured taps
 * (CONFIG_TETRA_EQ_INIT) rather than a pass-through.
 */
void tetra_requestReset(bool seedEqualizer);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_TETRA_STATUS_H */
