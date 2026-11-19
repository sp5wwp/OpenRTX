/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Host harness for the C62 TETRA receive chain: runs TetraDemod.c and
 * libtetra on 48 kHz discriminator audio (a C62 baseband dump or a
 * simulation) the way OpMode_TETRA does, and prints figures of merit.
 *
 *   cc -O2 -I openrtx/include -I <libtetra>/include -I <libtetra>/build/generated
 *      tests/platform/c62_tetra_host.c openrtx/src/protocols/TETRA/TetraDemod.c
 *      <libtetra>/build/libtetra.a -lm -o c62_tetra_host
 *   c62_tetra_host [-r] [-v] [-e] input.wav|input.s16
 *
 *  -r  on a demodulator jump call tetra_tmo_resync() instead of re-init
 *  -e  do not seed the equalizer with CONFIG_TETRA_EQ_INIT
 *  -v  print libtetra events
 *  -t  trace the demodulator every 0.48 s
 *  -g  input gain (float), applied before the demodulator
 *  -P  name=value: set a demodulator parameter (tetraDemodParams)
 */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <tetra/tetra.h>
#include "protocols/TETRA/TetraDemod.h"

#define HALF_SAMPLES 1440

static const float eqInit[TETRA_DEMOD_EQ_TAPS] = { 0.007f,  -0.004f, 0.036f,
                                                   -0.185f, 1.149f,  -0.187f,
                                                   0.033f,  -0.001f, 0.0f };

static struct tetraDemod demod;
static struct tetra_tmo_rx tmo;
static int verbose;
static unsigned nTraffic, nCalls, nSds;
#define ERR_LOG_MAX 100000
static unsigned short errLog[ERR_LOG_MAX];
static unsigned nFrames, nBadFrames; /* Clear speech frames, CRC failed */
static struct tetra_speech_ws speechWs;
static int trace;
static uint32_t crcTotal;

static void onEvent(void *user, const struct tetra_event *ev)
{
    (void)user;
    switch (ev->type) {
        case TETRA_EV_TRAFFIC: {
            nTraffic++;
            const struct tetra_traffic *t = &ev->u.traffic;
            if (!t->encrypted && t->circuit_type == 0) {
                struct tetra_speech sp;
                tetra_speech_decode(&speechWs, t, &sp);
                nFrames += 2;
                nBadFrames += sp.bfi[0] + sp.bfi[1];
            }
            break;
        }
        case TETRA_EV_CALL:
            nCalls++;
            break;
        case TETRA_EV_SDS:
            nSds++;
            break;
        default:
            break;
    }
    if (verbose && ev->type != TETRA_EV_TRAFFIC)
        printf("ev %d\n", ev->type);
}

static void initTmo(void)
{
    struct tetra_rx_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cb = onEvent;
    cfg.promiscuous = true;
    tetra_tmo_init(&tmo, &cfg);
}

/* "name=value": set a tetraDemodParams field */
static bool setParam(const char *arg)
{
    static const struct {
        const char *name;
        float *field;
    } fields[] = {
        { "costAlpha", &tetraDemodParams.costAlpha },
        { "timingKp", &tetraDemodParams.timingKp },
        { "timingKi", &tetraDemodParams.timingKi },
        { "fitAlpha", &tetraDemodParams.fitAlpha },
        { "eqMu", &tetraDemodParams.eqMu },
        { "softScale", &tetraDemodParams.softScale },
        { "eqBiasMu", &tetraDemodParams.eqBiasMu },
        { "fitHold", &tetraDemodParams.fitHold },
        { "acqEnergy", &tetraDemodParams.acqEnergy },
        { "okHold", &tetraDemodParams.okHold },
        { "ctxMu", &tetraDemodParams.ctxMu },
        { "ctxSpan", &tetraDemodParams.ctxSpan },
    };
    const char *eq = strchr(arg, '=');
    if (eq == NULL)
        return false;
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        if (strlen(fields[i].name) == (size_t)(eq - arg)
            && !strncmp(arg, fields[i].name, eq - arg)) {
            *fields[i].field = strtof(eq + 1, NULL);
            return true;
        }
    }
    return false;
}

int main(int argc, char **argv)
{
    int useResync = 0, seedEq = 1, opt;
    float inGain = 1.0f;
    while ((opt = getopt(argc, argv, "rvteg:P:")) != -1) {
        switch (opt) {
            case 'r':
                useResync = 1;
                break;
            case 'v':
                verbose = 1;
                break;
            case 't':
                trace = 1;
                break;
            case 'e':
                seedEq = 0;
                break;
            case 'g':
                inGain = strtof(optarg, NULL);
                break;
            case 'P':
                if (!setParam(optarg)) {
                    fprintf(stderr, "bad -P %s\n", optarg);
                    return 2;
                }
                break;
            default:
                fprintf(stderr, "usage: %s [-r] [-v] [-e] [-g gain] in\n",
                        argv[0]);
                return 2;
        }
    }
    if (optind >= argc)
        return 2;
    FILE *f = fopen(argv[optind], "rb");
    if (!f) {
        perror(argv[optind]);
        return 1;
    }
    /* WAV: skip to the data chunk; raw s16 otherwise */
    char hdr[12];
    if (fread(hdr, 1, 12, f) == 12 && !memcmp(hdr, "RIFF", 4)) {
        char id[4];
        uint32_t len;
        while (fread(id, 1, 4, f) == 4 && fread(&len, 4, 1, f) == 1) {
            if (!memcmp(id, "data", 4))
                break;
            fseek(f, len, SEEK_CUR);
        }
    } else {
        fseek(f, 0, SEEK_SET);
    }

    tetraDemod_init(&demod);
    if (seedEq)
        tetraDemod_setEqualizer(&demod, eqInit);
    initTmo();

    static int16_t buf[HALF_SAMPLES];
    static int8_t soft[HALF_SAMPLES * 3 / 4 + 8];
    uint32_t jumps = 0, samples = 0, inits = 1, lastCrc = 0;
    double errSum = 0;
    unsigned errN = 0, syncBlocks = 0, blocks = 0;
    size_t n;
    while ((n = fread(buf, 2, HALF_SAMPLES, f)) > 0) {
        if (inGain != 1.0f) {
            for (size_t i = 0; i < n; i++) {
                float v = buf[i] * inGain;
                buf[i] = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
            }
        }
        size_t k = tetraDemod_process(&demod, buf, n, soft);
        uint32_t j = tetraDemod_reacquisitions(&demod);
        if (j != jumps) {
            jumps = j;
            if (useResync) {
                tetra_tmo_resync(&tmo);
            } else {
                crcTotal += tetra_tmo_crc_ok(&tmo);
                initTmo();
                inits++;
            }
        }
        tetra_tmo_push_soft(&tmo, soft, k);
        /* As OpMode_TETRA: good blocks keep the demodulator's lock */
        uint32_t crcNow = crcTotal + tetra_tmo_crc_ok(&tmo);
        if (crcNow != lastCrc) {
            lastCrc = crcNow;
            tetraDemod_signalOk(&demod);
        }
        samples += n;
        blocks++;
        if (samples > 48000) {
            errSum += tetraDemod_phaseError(&demod);
            errN++;
            if (errN <= ERR_LOG_MAX)
                errLog[errN - 1] = tetraDemod_phaseError(&demod);
        }
        if (tetra_tmo_locked(&tmo))
            syncBlocks++;
        if (trace && blocks % 16 == 0)
            printf("%6.2f err %3u fast %3.0f cfo %5d gain %.3g reacq %u "
                   "slips %u lock %d crc %u eq %.3f %.3f %.3f\n",
                   samples / 48000.0, tetraDemod_phaseError(&demod),
                   100 * sqrtf(demod.errFast) / 0.785398f,
                   tetraDemod_carrierOffset(&demod), demod.gain,
                   demod.reacquired, demod.slips, tetra_tmo_locked(&tmo),
                   tetra_tmo_crc_ok(&tmo), demod.eq[3], demod.eq[4],
                   demod.eq[5]);
    }
    crcTotal += tetra_tmo_crc_ok(&tmo);
    /* Steady state: mean phase error over the second half of the run */
    unsigned logged = errN < ERR_LOG_MAX ? errN : ERR_LOG_MAX;
    double errLate = 0;
    for (unsigned i = logged / 2; i < logged; i++)
        errLate += errLog[i];
    if (logged > 1)
        errLate /= logged - logged / 2;
    double secs = samples / 48000.0;
    printf("secs %.1f crc %u crc/s %.1f sync %.0f%% err %.1f%% "
           "reacq %u slips %u inits %u realign %u cfo %d clk %d "
           "traffic %u calls %u sds %u eq %.3f %.3f %.3f biasHz %.0f "
           "frames %u bad %u errLate %.2f%%\n",
           secs, crcTotal, crcTotal / secs, 100.0 * syncBlocks / blocks,
           errN ? errSum / errN : 0, demod.reacquired, demod.slips, inits,
           tetra_tmo_realigned(&tmo, NULL), tetraDemod_carrierOffset(&demod),
           tetraDemod_clockOffset(&demod), nTraffic, nCalls, nSds, demod.eq[3],
           demod.eq[4], demod.eq[5], demod.eqBias * 18000 / (2 * 3.14159265),
           nFrames, nBadFrames, errLate);
    return 0;
}
