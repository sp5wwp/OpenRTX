/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <hwconfig.h>
#include <tetra/tetra.h>
#include "interfaces/audio.h"
#include "interfaces/delays.h"
#include "interfaces/platform.h"
#include "interfaces/radio.h"
#include "protocols/TETRA/TetraDemod.h"
#include "protocols/TETRA/TetraSpeech.h"
#include "protocols/TETRA/TetraStatus.h"
#include "rtx/OpMode_TETRA.hpp"
#ifdef CONFIG_TETRA_AFC
#include "core/state.h"
#endif

/* 30 ms per half buffer: room for libtetra's per-slot decoding */
#define HALF_SAMPLES 1440

/* Samples dropped after starting or retuning: 0.5 s */
#define SETTLE_SAMPLES (TETRA_DEMOD_IN_RATE / 2)

/* Large state: static, there is only one TETRA mode */
static int16_t baseband[2 * HALF_SAMPLES];
static int8_t soft[HALF_SAMPLES * 3 / 4 + 8];
static struct tetraDemod demod;
static struct tetra_tmo_rx tmo;

/* Diagnostics and remote control (see TetraStatus.h) */
static uint32_t crcBase; /* CRC passes of earlier tmo contexts */
static uint32_t lastCrc;
#ifdef CONFIG_TETRA_AFC
/*
 * Automatic frequency correction: every AFC_PERIOD_MS, the mean carrier
 * offset measured by the demodulator while blocks decode moves the
 * receiver's fine tuning towards a zero reading. That reading is about
 * 0.15 of the real offset (the receive path removes most of the carrier's
 * DC), but it is monotonic and zero where the phase error is lowest; the
 * loop gain allows for it. The offset found is kept across retunes: it is
 * mostly the radio's own (crystal, filters).
 */
#define AFC_PERIOD_MS 2000
#define AFC_GAIN 3       /* Hz of correction per Hz of mean reading */
#define AFC_STEP_MAX 300 /* Hz per period */
#define AFC_DEADBAND 25  /* Hz of mean reading */
#define AFC_RANGE 4000   /* Hz, either way: the C62 decodes about 4 kHz off */
#define AFC_CRC_RECENT_MS 1000
/*
 * Readings are trusted below this phase error (% of pi/4): near the edges
 * of the band where the C62 decodes (above 30%), the offset reading can
 * have the wrong sign
 */
#define AFC_ERROR_MAX 35
/* Nothing decoded this long: go back to no correction, and start again */
#define AFC_LOST_MS 8000
static int32_t afcOffset;
static float afcSum;
static uint32_t afcCount;
static long long afcStart;
static long long lastCrcTick;
static int32_t afcApplied;

static bool afcEnabled(void)
{
    return state.settings.tetraAfcOff == 0;
}

/**
 * Apply an offset to the receiver, if it is not applied already.
 */
static void afcApply(int32_t offset)
{
    if (offset != afcApplied) {
        radio_setRxOffset(offset);
        afcApplied = offset;
    }
}

/**
 * Called after each block of samples.
 */
static void afcUpdate(void)
{
    if (!afcEnabled()) {
        afcApply(0);
        afcSum = 0;
        afcCount = 0;
        return;
    }
    afcApply(afcOffset); /* Turned on again */

    /* Measure only while blocks decode: never steer on noise */
    long long now = getTick();
    if (tetra_tmo_locked(&tmo) && (now - lastCrcTick) < AFC_CRC_RECENT_MS
        && tetraDemod_phaseError(&demod) < AFC_ERROR_MAX) {
        afcSum += tetraDemod_carrierOffset(&demod);
        afcCount++;
    }

    /*
     * Lost for long, maybe because the correction is wrong for this signal
     * (another cell, a jump): from no correction it can be found again
     */
    if ((now - lastCrcTick) > AFC_LOST_MS && afcOffset != 0) {
        afcOffset = 0;
        afcApply(0);
        afcSum = 0;
        afcCount = 0;
        afcStart = now;
        return;
    }
    if ((now - afcStart) < AFC_PERIOD_MS)
        return;
    afcStart = now;

    uint32_t count = afcCount;
    float mean = (count > 0) ? afcSum / count : 0;
    afcSum = 0;
    afcCount = 0;
    if (count < 10 || fabsf(mean) <= AFC_DEADBAND)
        return;

    float step = AFC_GAIN * mean;
    if (step > AFC_STEP_MAX)
        step = AFC_STEP_MAX;
    else if (step < -AFC_STEP_MAX)
        step = -AFC_STEP_MAX;
    int32_t next = afcOffset + (int32_t)lrintf(step);
    if (next > AFC_RANGE)
        next = AFC_RANGE;
    else if (next < -AFC_RANGE)
        next = -AFC_RANGE;
    afcOffset = (next / 10) * 10; /* The BK4819 tunes in 10 Hz steps */
    afcApply(afcOffset);
}
#endif                              /* CRC passes the demodulator was told of */
static volatile bool resetRequest;
static volatile bool eqSeed = true; /* Start from CONFIG_TETRA_EQ_INIT */

OpMode_TETRA::OpMode_TETRA()
    : startRx(false)
    , dataErrors(0)
    , settle(SETTLE_SAMPLES)
    , demodJumps(0)
    , basebandPath(-1)
    , basebandId(-1)
{
}

OpMode_TETRA::~OpMode_TETRA()
{
    disable();
}

void OpMode_TETRA::reset()
{
    settle = SETTLE_SAMPLES;
    tetraDemod_init(&demod);
#ifdef CONFIG_TETRA_EQ_INIT
    static const float eqInit[TETRA_DEMOD_EQ_TAPS] = CONFIG_TETRA_EQ_INIT;
    if (eqSeed)
        tetraDemod_setEqualizer(&demod, eqInit);
#endif

    demodJumps = 0;
    resync(false);
    crcBase = 0;
    lastCrc = 0;
    tetraStatus_reset();
}

/*
 * Restart libtetra's burst synchronisation. After a jump in the symbol
 * stream (samples lost, the demodulator re-acquiring) it would otherwise
 * keep decoding on the old slot grid for seconds before noticing.
 */
void OpMode_TETRA::resync(bool overrun)
{
    crcBase += tetra_tmo_crc_ok(&tmo);

    struct tetra_rx_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cb = tetraStatus_onEvent;
    cfg.promiscuous = true;
    tetra_tmo_init(&tmo, &cfg);
    tetraStatus_lostSync(overrun);
}

void OpMode_TETRA::enable()
{
#ifdef CONFIG_TETRA_AFC
    /* Before receiving starts: no retune once it has settled */
    afcApply(afcEnabled() ? afcOffset : 0);
    afcSum = 0;
    afcCount = 0;
    afcStart = getTick();
    lastCrcTick = afcStart; /* Not "lost" before trying */
#endif
    reset();
    tetraSpeech_start();
    startRx = true;
}

void OpMode_TETRA::disable()
{
    startRx = false;
    stopSampling();
    tetraSpeech_stop();
#ifdef CONFIG_TETRA_AFC
    afcApply(0); /* The other modes tune where they are told */
#endif
    radio_disableRtx();
    platform_ledOff(GREEN);
    tetraStatus_reset();
}

void OpMode_TETRA::startSampling()
{
    basebandPath = audioPath_request(SOURCE_RTX, SINK_MCU, PRIO_RX);
    basebandId = audioStream_start(basebandPath, baseband, 2 * HALF_SAMPLES,
                                   TETRA_DEMOD_IN_RATE,
                                   STREAM_INPUT | BUF_CIRC_DOUBLE);
}

void OpMode_TETRA::stopSampling()
{
    if (basebandId >= 0)
        audioStream_terminate(basebandId);
    if (basebandPath >= 0)
        audioPath_release(basebandPath);
    basebandId = -1;
    basebandPath = -1;
}

void OpMode_TETRA::update(rtxStatus_t *const rtx, const bool newCfg)
{
    switch (rtx->opStatus) {
        case OFF:
            radio_disableRtx();
            if (startRx) {
                rtx->opStatus = RX;
                return;
            }
            sleepFor(0, 30);
            return;

        case RX:
            break;

        default:
            /* Receive only */
            rtx->opStatus = OFF;
            startRx = true;
            return;
    }

    if (startRx) {
        startSampling();
        radio_enableRx();
        startRx = false;
    }

    /* A new frequency is a new cell: start from scratch */
    if (newCfg || resetRequest) {
        resetRequest = false;
        reset();
    }

    if (audioPath_getStatus(basebandPath) != PATH_OPEN) {
        sleepFor(0, 10);
        return;
    }

    /*
     * Wait for the next half buffer of samples. A failed read means we were
     * late and samples were lost: the symbol stream jumped, so resync, and
     * do not lose more by sleeping (unless the stream keeps failing).
     */
    dataBlock_t block = inputStream_getData(basebandId);
    if (block.data == NULL) {
        if (dataErrors++ == 0)
            resync(true);
        else
            sleepFor(0, 10);
        return;
    }
    dataErrors = 0;

    /*
     * Drop what follows a start or a retune: the receiver and the audio
     * path settle, and the demodulator would take their transient for the
     * carrier offset and keep it.
     */
    if (settle > 0) {
        settle -= (block.len < settle) ? block.len : settle;
        return;
    }

    size_t n = tetraDemod_process(&demod, block.data, block.len, soft);

    /* The demodulator followed a timing jump or re-acquired: resync too */
    uint32_t jumps = tetraDemod_reacquisitions(&demod);
    if (jumps != demodJumps) {
        demodJumps = jumps;
        resync(false);
    }

    tetra_tmo_push_soft(&tmo, soft, n);

    /* Blocks decode: the demodulator keeps its lock */
    uint32_t crc = crcBase + tetra_tmo_crc_ok(&tmo);
    if (crc != lastCrc) {
        lastCrc = crc;
        tetraDemod_signalOk(&demod);
#ifdef CONFIG_TETRA_AFC
        lastCrcTick = getTick();
#endif
    }
#ifdef CONFIG_TETRA_AFC
    afcUpdate();
#endif

    tetraStatus_setDemod(tetraDemod_phaseError(&demod),
                         tetraDemod_carrierOffset(&demod));

    if (tetraStatus_sync())
        platform_ledOn(GREEN);
    else
        platform_ledOff(GREEN);
}

void tetra_getDiag(struct tetraDiag *out)
{
    out->crcOk = crcBase + tetra_tmo_crc_ok(&tmo);
    out->realigned = tetra_tmo_realigned(&tmo, NULL);
    out->locked = tetra_tmo_locked(&tmo);
    out->reacquired = demod.reacquired;
    out->slips = demod.slips;
    out->symbols = demod.symbols;
    out->phaseErr = tetraDemod_phaseError(&demod);
    out->carrierHz = tetraDemod_carrierOffset(&demod);
    out->clockPpm = tetraDemod_clockOffset(&demod);
    out->eqSeed = eqSeed;
    memcpy(out->eq, demod.eq, sizeof(out->eq));
}

void tetra_requestReset(bool seedEqualizer)
{
    eqSeed = seedEqualizer;
    resetRequest = true;
}
