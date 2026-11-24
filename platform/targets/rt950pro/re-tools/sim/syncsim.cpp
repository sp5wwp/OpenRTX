/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Host simulation: M17 voice transmissions through white noise, the real
// demodulator and frame decoder; where frames are lost.
#include <cstdio>
#include <cstring>
#include <cmath>
#include <map>
#include <random>
#include <vector>
#include <cstdlib>
#include <string>
#include "core/audio_path.h"
#include "core/audio_stream.h"
#include "protocols/M17/Demodulator.hpp"
#include "protocols/M17/FrameDecoder.hpp"
#include "M17Signal.hpp"
using namespace M17;



struct Noise {
    std::mt19937 rng;
    std::normal_distribution<double> g;
    std::uniform_real_distribution<double> u{0.0, 1.0};
    bool parabolic;
    double prev = 0.0;
    double clickProb;        // per sample
    double dc = 0.0;         // carrier offset, 2.5 per Hz (2000 per 800 Hz unit)
    double pending[3] = {0, 0, 0};
    Noise(unsigned seed, double rms) : rng(seed), g(0.0, rms), parabolic(getenv("PARABOLIC") != nullptr)
    {
        const char *c = getenv("CLICKS");    // clicks per second
        clickProb = c ? atof(c) / 24000.0 : 0.0;
        const char *o = getenv("OFFSET_HZ");
        dc = o ? atof(o) * 2.5 : 0.0;
    }
    double operator()() {
        double w = g(rng);
        double v = w;
        if (parabolic) { v = (w - prev) / std::sqrt(2.0); prev = w; }
        // A 2 pi phase slip: area 2 pi * 8000, over three samples
        if ((clickProb > 0) && (u(rng) < clickProb)) {
            double area = ((u(rng) < 0.5) ? -1.0 : 1.0) * 6.2831853 * 8000.0;
            pending[0] += area / 4; pending[1] += area / 2; pending[2] += area / 4;
        }
        v += pending[0] + dc;
        pending[0] = pending[1]; pending[1] = pending[2]; pending[2] = 0;
        return v;
    }
};

static softFrame_t clean(const frame_t &f)
{
    softFrame_t s;
    for (size_t i = 0; i < s.size(); i++)
        s[i] = getBit(f, i) ? UINT16_MAX : 0;
    return s;
}


static void noiseOnly(double rms, double seconds)
{
    Noise g(77, rms);
    Demodulator demod; demod.init();
    FrameDecoder dec;
    long locks = 0, frames = 0, stream = 0, accepted = 0;
    bool locked = false;
    const long n = (long)(seconds * 24000);
    for (long i = 0; i < n; i++) {
        double v = std::max(-32768.0, std::min(32767.0, g()));
        bool nf = demod.sample((int16_t)v);
        bool l = demod.isLocked();
        if (l && !locked) { locks++; dec.reset(); }
        locked = l;
        if (nf && l) {
            frames++;
            uint32_t a0 = m17StreamStats.accepted;
            if (dec.decodeFrame(demod.getSoftFrame()) == FrameType::STREAM) stream++;
            if (m17StreamStats.accepted != a0) accepted++;
        }
    }
    demod.terminate();
    printf("noise rms %6.0f: %5.1f locks/min, %5.1f frames/min (%5.1f as stream), %ld accepted in %.0f s\n",
           rms, locks * 60.0 / seconds, frames * 60.0 / seconds, stream * 60.0 / seconds, accepted, seconds);
}

int main(int argc, char **argv)
{
    if ((argc > 1) && (std::string(argv[1]) == "noise")) {
        for (int i = 2; i < argc; i++) noiseOnly(atof(argv[i]), 600);
        return 0;
    }

    const int TX = 20, FRAMES = 50;
    std::vector<frame_t> frames = M17Signal::voiceFrames(FRAMES);
    // Reference payloads by frame number
    std::map<uint16_t, std::vector<uint8_t>> ref;
    {
        FrameDecoder d;
        for (auto &f : frames)
            if (d.decodeFrame(clean(f)) == FrameType::STREAM) {
                StreamFrame sf = d.getStreamFrame();
                ref[sf.getFrameNumber() & 0x7FFF] =
                    std::vector<uint8_t>(sf.data(), sf.data() + 16);
            }
    }
    std::vector<int8_t> syms;
    for (int k = 0; k < 120; k++) syms.push_back(0);   // silence before
    auto s = M17Signal::m17Symbols(frames);
    syms.insert(syms.end(), s.begin(), s.end());
    for (int k = 0; k < 120; k++) syms.push_back(0);
    std::vector<int16_t> clean_bb = M17Signal::rrcBaseband(syms, 2000.0f);

    printf("noise rms | sent | delivered | stream | accepted right | wrong | lock drops\n");
    for (int ai = 1; ai < argc; ai++) {
        const double rms = atof(argv[ai]);
        Noise g(1234, rms);
        long sent = 0, deliv = 0, stream = 0, right = 0, wrong = 0, drops = 0;
        for (int t = 0; t < TX; t++) {
            Demodulator demod; demod.init();
            FrameDecoder dec;
            bool locked = false, wasLocked = false;
            uint32_t acc0 = m17StreamStats.accepted;
            for (int16_t x : clean_bb) {
                double v = x + g();
                v = std::max(-32768.0, std::min(32767.0, v));
                bool nf = demod.sample((int16_t)v);
                bool l = demod.isLocked();
                if (wasLocked && !l) drops++;
                if (l && !locked) { dec.reset(); }
                locked = l; wasLocked = l;
                if (nf && l) {
                    deliv++;
                    uint32_t a0 = m17StreamStats.accepted;
                    auto type = dec.decodeFrame(demod.getSoftFrame());
                    if (type == FrameType::STREAM) {
                        stream++;
                        if (m17StreamStats.accepted != a0) {
                            StreamFrame sf = dec.getStreamFrame();
                            auto it = ref.find(sf.getFrameNumber() & 0x7FFF);
                            if (it != ref.end() && memcmp(it->second.data(), sf.data(), 16) == 0) right++;
                            else wrong++;
                        }
                    }
                }
            }
            (void)acc0;
            demod.terminate();
            sent += FRAMES;
            drops -= 1;  // the drop at the end of the transmission is expected
        }
        printf("%8.0f | %4ld | %9ld | %6ld | %6ld (%4.1f %%) | %5ld | %ld\n", rms, sent, deliv,
               stream, right, 100.0 * right / sent, wrong, drops);
    }
    return 0;
}
