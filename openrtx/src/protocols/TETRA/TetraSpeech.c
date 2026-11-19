/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "protocols/TETRA/TetraSpeech.h"

#ifdef CONFIG_TETRA_SPEECH

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <openacelp.h>
#include <tetra/tetra.h>
#include "core/audio_path.h"
#include "core/audio_stream.h"
#include "interfaces/delays.h"

#define QUEUE_FRAMES 16     /* 480 ms */
#define PREBUFFER_FRAMES 5  /* 150 ms: frame 18 leaves a 113 ms gap */
#define SILENCE_FRAMES 34   /* About one second of 30 ms frames */
#define THREAD_STACK 4096
#define FOLLOW_HOLD_MS 1000 /* Keep following a timeslot this long */

struct frame {
    uint8_t bits[OA_FRAME_BITS];
    uint8_t bfi;
};

/* Queue from the rtx thread (libtetra callback) to the playback thread */
static pthread_mutex_t queueMutex = PTHREAD_MUTEX_INITIALIZER;
static struct frame queue[QUEUE_FRAMES];
static unsigned queueHead;
static unsigned queueCount;

static struct tetra_speech_ws channelWs; /* rtx thread only */
static volatile uint32_t statFrames;     /* Speech frames decoded */
static volatile uint32_t statBad;        /* Of which failed the CRC (BFI) */
static volatile uint32_t statOtherTs;    /* Slots ignored: other timeslot */
/* Playback statistics, playback thread only */
static volatile uint32_t playFrames;   /* Frames played */
static volatile uint32_t playBursts;   /* Playback bursts begun */
static volatile uint32_t playStreams;  /* Output streams started and synced */
static volatile uint32_t playBusy;     /* Speaker path refused */
static volatile uint32_t playNoStream; /* Output stream did not start */
static volatile uint32_t playNoBuffer; /* No output buffer: burst cut */
static uint8_t followTn;               /* rtx thread only, 0 = none */
static long long followLast;           /* rtx thread only, ms */
static oa_dec_t decoder;               /* Playback thread only */
static stream_sample_t pcm[2 * OA_FRAME_SAMPLES];

static pthread_t thread;
static pthread_attr_t attr;
static volatile bool running;
static volatile bool stopRequest;

static unsigned queued(void)
{
    pthread_mutex_lock(&queueMutex);
    unsigned n = queueCount;
    pthread_mutex_unlock(&queueMutex);
    return n;
}

static bool pop(struct frame *f)
{
    bool ok = false;
    pthread_mutex_lock(&queueMutex);
    if (queueCount > 0) {
        *f = queue[queueHead];
        queueHead = (queueHead + 1) % QUEUE_FRAMES;
        queueCount--;
        ok = true;
    }
    pthread_mutex_unlock(&queueMutex);
    return ok;
}

/**
 * Play queued speech until a second of silence, then release the speaker.
 */
static void playBurst(void)
{
    playBursts++;
    pathId path = audioPath_request(SOURCE_MCU, SINK_SPK, PRIO_RX);
    if (audioPath_getStatus(path) != PATH_OPEN) {
        playBusy++;
        audioPath_release(path);
        sleepFor(0, 100);
        return;
    }

    memset(pcm, 0, sizeof(pcm));
    streamId stream = audioStream_start(path, pcm, 2 * OA_FRAME_SAMPLES, 8000,
                                        STREAM_OUTPUT | BUF_CIRC_DOUBLE);
    if (stream < 0) {
        playNoStream++;
        audioPath_release(path);
        sleepFor(0, 100);
        return;
    }

    oa_dec_init(&decoder, &oa_tables_tetra);
    outputStream_sync(stream, false);
    playStreams++;

    unsigned silence = 0;
    while (!stopRequest && silence < SILENCE_FRAMES
           && audioPath_getStatus(path) == PATH_OPEN) {
        stream_sample_t *out = outputStream_getIdleBuffer(stream);
        if (out == NULL) {
            playNoBuffer++;
            break;
        }

        struct frame f;
        if (pop(&f)) {
            oa_dec_frame(&decoder, f.bits, f.bfi, out);
            playFrames++;
            silence = 0;
        } else {
            memset(out, 0, OA_FRAME_SAMPLES * sizeof(*out));
            silence++;
        }
        outputStream_sync(stream, true);
    }

    audioStream_stop(stream);
    audioPath_release(path);
}

static void *playback(void *arg)
{
    (void)arg;
    while (!stopRequest) {
        if (queued() >= PREBUFFER_FRAMES)
            playBurst();
        else
            sleepFor(0, 10);
    }
    return NULL;
}

void tetraSpeech_start(void)
{
    if (running)
        return;

    pthread_mutex_lock(&queueMutex);
    queueHead = queueCount = 0;
    pthread_mutex_unlock(&queueMutex);
    followTn = 0;
    stopRequest = false;

    pthread_attr_init(&attr);
#ifdef __ZEPHYR__
    void *stack = malloc(THREAD_STACK);
    if (stack == NULL)
        return;
    pthread_attr_setstack(&attr, stack, THREAD_STACK);
#else
    pthread_attr_setstacksize(&attr, THREAD_STACK);
#endif
    running = pthread_create(&thread, &attr, playback, NULL) == 0;
#ifdef __ZEPHYR__
    if (!running)
        free(stack);
#endif
}

void tetraSpeech_stop(void)
{
    if (!running)
        return;

    stopRequest = true;
    pthread_join(thread, NULL);
    running = false;
#ifdef __ZEPHYR__
    void *stack;
    size_t size;
    pthread_attr_getstack(&attr, &stack, &size);
    free(stack);
#endif
}

void tetraSpeech_traffic(const struct tetra_traffic *t)
{
    if (!running || t->encrypted || t->circuit_type != 0)
        return;

    /*
     * One decoder plays one stream: follow the timeslot of the first speech
     * heard and ignore the others, or two calls on different timeslots would
     * be interleaved frame by frame. Move on once the followed timeslot has
     * been quiet for as long as the speaker is held after speech.
     */
    long long now = getTick();
    if (followTn != 0 && t->time.tn != followTn
        && now - followLast < FOLLOW_HOLD_MS) {
        statOtherTs++;
        return;
    }
    followTn = t->time.tn;
    followLast = now;

    struct tetra_speech sp;
    tetra_speech_decode(&channelWs, t, &sp);

    pthread_mutex_lock(&queueMutex);
    for (int i = 0; i < 2; i++) {
        /* On overflow drop the oldest frame */
        if (queueCount == QUEUE_FRAMES) {
            queueHead = (queueHead + 1) % QUEUE_FRAMES;
            queueCount--;
        }
        struct frame *f = &queue[(queueHead + queueCount) % QUEUE_FRAMES];
        memcpy(f->bits, sp.bits[i], OA_FRAME_BITS);
        f->bfi = sp.bfi[i];
        queueCount++;
        statFrames++;
        if (sp.bfi[i])
            statBad++;
    }
    pthread_mutex_unlock(&queueMutex);
}

void tetraSpeech_playStats(uint32_t *played, uint32_t *busy, uint32_t *noStream,
                           uint32_t *noBuffer, uint32_t *bursts,
                           uint32_t *streams)
{
    *bursts = playBursts;
    *streams = playStreams;
    *played = playFrames;
    *busy = playBusy;
    *noStream = playNoStream;
    *noBuffer = playNoBuffer;
}

void tetraSpeech_stats(uint32_t *frames, uint32_t *bad, uint32_t *otherTs)
{
    *frames = statFrames;
    *bad = statBad;
    *otherTs = statOtherTs;
}

#else  /* CONFIG_TETRA_SPEECH */

void tetraSpeech_stats(uint32_t *frames, uint32_t *bad, uint32_t *otherTs)
{
    *frames = *bad = *otherTs = 0;
}

void tetraSpeech_playStats(uint32_t *played, uint32_t *busy, uint32_t *noStream,
                           uint32_t *noBuffer, uint32_t *bursts,
                           uint32_t *streams)
{
    *played = *busy = *noStream = *noBuffer = *bursts = *streams = 0;
}

void tetraSpeech_start(void)
{
}

void tetraSpeech_stop(void)
{
}

void tetraSpeech_traffic(const struct tetra_traffic *t)
{
    (void)t;
}

#endif /* CONFIG_TETRA_SPEECH */
