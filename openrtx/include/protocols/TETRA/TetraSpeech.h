/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_TETRA_SPEECH_H
#define OPENRTX_TETRA_SPEECH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct tetra_traffic;

/*
 * TETRA speech playback: libtetra's traffic slots -> speech frames (channel
 * decoding) -> OpenACELP -> 8 kHz audio on the speaker.
 *
 * A playback thread runs while the TETRA mode is enabled. It opens the
 * speaker path when speech arrives and releases it after one second of
 * silence.
 *
 * Built with CONFIG_TETRA_SPEECH (CMake option C62_TETRA_SPEECH); without it
 * these functions do nothing.
 */

/**
 * Start the playback thread.
 */
void tetraSpeech_start(void);

/**
 * Stop the playback thread and release the speaker.
 */
void tetraSpeech_stop(void);

/**
 * Queue the speech frames of a traffic slot. Call from the libtetra event
 * callback; encrypted and data slots are ignored.
 */
void tetraSpeech_traffic(const struct tetra_traffic *t);

/**
 * Speech statistics since boot, for test and remote control.
 *
 * @param frames: speech frames decoded (two per traffic slot).
 * @param bad: of which failed the CRC and were concealed.
 * @param otherTs: traffic slots ignored, from a timeslot not followed.
 */
void tetraSpeech_stats(uint32_t *frames, uint32_t *bad, uint32_t *otherTs);

/**
 * Speech playback statistics since boot, for test and remote control.
 *
 * @param played: speech frames played on the speaker.
 * @param busy: times the speaker path was refused (another path holds it).
 * @param noStream: times the output stream did not start.
 * @param noBuffer: times a burst ended because no output buffer came.
 */
void tetraSpeech_playStats(uint32_t *played, uint32_t *busy, uint32_t *noStream,
                           uint32_t *noBuffer);

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_TETRA_SPEECH_H */
