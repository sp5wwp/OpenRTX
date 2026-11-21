/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <new>
#include <cstddef>
#include <cstring>
#include "protocols/M17/Modulator.hpp"
#include "protocols/M17/Utils.hpp"
#include "protocols/M17/DSP.hpp"

#if defined(PLATFORM_LINUX)
#include <stdio.h>
#endif

using namespace M17;


Modulator::Modulator()
{

}

Modulator::~Modulator()
{
    terminate();
}

void Modulator::init()
{
    /*
     * Allocate a chunk of memory to contain two complete buffers for baseband
     * audio.
     */

    baseband_buffer = std::make_unique< int16_t[] >(2 * FRAME_SAMPLES);
    idleBuffer      = baseband_buffer.get();
    txRunning       = false;
    #if defined(PLATFORM_MD3x0) || defined(PLATFORM_MDUV3x0)
    pwmComp.reset();
    #endif
}

void Modulator::terminate()
{
    // Terminate an ongoing stream, if present
    if(txRunning)
    {
        audioStream_terminate(outStream);
        txRunning = false;
    }

    // Always ensure that outgoing audio path is closed
    audioPath_release(outPath);

    // Deallocate memory.
    baseband_buffer.reset();
}

bool Modulator::start()
{
    if(txRunning)
        return true;

    symbolHistory.fill(0.0f);

    #ifndef PLATFORM_LINUX
    outPath = audioPath_request(SOURCE_MCU, SINK_RTX, PRIO_TX);
    if(outPath < 0)
        return false;

    outStream = audioStream_start(outPath, baseband_buffer.get(),
                                  2*FRAME_SAMPLES, TX_SAMPLE_RATE,
                                  STREAM_OUTPUT | BUF_CIRC_DOUBLE);

    if(outStream < 0)
        return false;

    idleBuffer = outputStream_getIdleBuffer(outStream);
    #endif

    txRunning = true;

    return true;
}

void Modulator::sendPreamble()
{
    // Fill symbol buffer with preamble, made of alternated +3 and -3 symbols
    for(size_t i = 0; i < symbols.size(); i += 2)
    {
        symbols[i]     = +3;
        symbols[i + 1] = -3;
    }

    // Generate and transmit a 160 ms preamble (four 40 ms frames).
    for (size_t frame = 0; frame < 4; frame++)
    {
        symbolsToBaseband();
        sendBaseband();
    }
}

void Modulator::sendFrame(const frame_t& frame)
{
    auto it = symbols.begin();
    for(size_t i = 0; i < frame.size(); i++)
    {
        auto sym = byteToSymbols(frame[i]);
        it       = std::copy(sym.begin(), sym.end(), it);
    }

    symbolsToBaseband();
    sendBaseband();
}

void Modulator::stop()
{
    if(txRunning == false)
        return;

    audioStream_stop(outStream);
    txRunning  = false;
    idleBuffer = baseband_buffer.get();
    audioPath_release(outPath);

    #if defined(PLATFORM_MD3x0) || defined(PLATFORM_MDUV3x0)
    pwmComp.reset();
    #endif
}

void Modulator::invertPhase(const bool status)
{
    invPhase = status;
}


/*
 * RRC pulse shaping of the symbols upsampled to 48 kHz. The upsampled stream
 * is zero but at one sample per symbol, so each output sample is computed from
 * the RRC_SYMBOLS taps aligned with a symbol (polyphase form): the same
 * output as filtering the zero-stuffed stream, with a tenth of the products.
 */
void Modulator::symbolsToBaseband()
{
    static constexpr size_t SPS  = SAMPLES_PER_SYMBOL;
    static constexpr size_t TAPS = M17::rrc_taps_48k.size();
    static_assert(TAPS <= (RRC_SYMBOLS * SPS),
                  "RRC_SYMBOLS too small for the 48 kHz RRC filter");

    for(size_t i = 0; i < symbols.size(); i++)
    {
        for(size_t j = RRC_SYMBOLS - 1; j > 0; j--)
            symbolHistory[j] = symbolHistory[j - 1];

        symbolHistory[0] = static_cast< float >(symbols[i]) * RRC_GAIN;

        for(size_t p = 0; p < SPS; p++)
        {
            float elem = 0.0f;
            for(size_t j = 0; (p + j * SPS) < TAPS; j++)
                elem += M17::rrc_taps_48k[p + j * SPS] * symbolHistory[j];

            elem -= RRC_OFFSET;
            #if defined(PLATFORM_MD3x0) || defined(PLATFORM_MDUV3x0)
            elem          = pwmComp(elem);
            #endif
            if(invPhase) elem = 0.0f - elem;    // Invert signal phase
            idleBuffer[(i * SPS) + p] = static_cast< int16_t >(elem);
        }
    }
}

#ifndef PLATFORM_LINUX
void Modulator::sendBaseband()
{
    if(txRunning == false) return;
    if(audioPath_getStatus(outPath) != PATH_OPEN) return;

    // Transmission is ongoing, syncronise with stream end before proceeding
    outputStream_sync(outStream, true);
    idleBuffer = outputStream_getIdleBuffer(outStream);
}
#else
void Modulator::sendBaseband()
{
    FILE *outfile = fopen("/tmp/m17_output.raw", "ab");

    for(size_t i = 0; i < FRAME_SAMPLES; i++)
    {
        auto s = idleBuffer[i];
        fwrite(&s, sizeof(s), 1, outfile);
    }

    fclose(outfile);
}
#endif
