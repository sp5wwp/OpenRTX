/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef OPENRTX_TETRA_SPEECH_H
#define OPENRTX_TETRA_SPEECH_H

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
 * OpenACELP needs the ETSI EN 300 395-2 codebooks, generated at build time
 * (CONFIG_TETRA_SPEECH). Without them these functions do nothing.
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

#ifdef __cplusplus
}
#endif

#endif /* OPENRTX_TETRA_SPEECH_H */
