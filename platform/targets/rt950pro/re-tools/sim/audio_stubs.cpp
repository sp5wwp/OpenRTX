/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/audio_path.h"
#include "core/audio_stream.h"
// The demodulator's live sampling is never started here
extern "C" {
pathId audioPath_request(enum AudioSource, enum AudioSink, enum AudioPriority) { return 0; }
enum PathStatus audioPath_getStatus(const pathId) { return PATH_CLOSED; }
void audioPath_release(const pathId) {}
streamId audioStream_start(const pathId, stream_sample_t * const, const size_t, const uint32_t, const uint8_t) { return -1; }
void audioStream_stop(const streamId) {}
void audioStream_terminate(const streamId) {}
dataBlock_t inputStream_getData(streamId) { return { nullptr, 0 }; }
stream_sample_t *outputStream_getIdleBuffer(const streamId) { return nullptr; }
bool outputStream_sync(const streamId, const bool) { return false; }
}

