/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Host model of the RT-950 Pro M17 receiver from its IF: FM at the 8 kHz low
// IF plus channel noise, the real at32_adc.c demodulator, the M17
// demodulator and decoder. Clicks arise from the noise by themselves.
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <random>
#include <vector>
#include <string>
#include "protocols/M17/Demodulator.hpp"
#include "protocols/M17/FrameDecoder.hpp"
#include "protocols/M17/DSP.hpp"
#include "M17Signal.hpp"
using namespace M17;
extern "C" void ifDemodReset(void);
extern "C" void ifDemod(const uint16_t *in, int16_t *out, size_t count);

static softFrame_t clean(const frame_t &f)
{
    softFrame_t s;
    for (size_t i = 0; i < s.size(); i++) s[i] = getBit(f, i) ? UINT16_MAX : 0;
    return s;
}

// Replay a capture from ifcap.py: little-endian uint16 ADC counts at 48 kHz
static int replay(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }
    std::vector<uint16_t> raw;
    uint16_t v;
    while (fread(&v, 2, 1, f) == 1) raw.push_back(v);
    fclose(f);
    ifDemodReset();
    Demodulator demod; demod.init();
    FrameDecoder dec;
    long deliv = 0, stream = 0, acc = 0, spikes = 0, locks = 0;
    bool locked = false;
    std::vector<int16_t> out(240);
    for (size_t i = 0; i + 480 <= raw.size(); i += 480) {
        ifDemod(&raw[i], out.data(), out.size());
        for (int16_t y : out) {
            if (std::abs(y) > 20000) spikes++;
            bool nfr = demod.sample(y);
            bool l = demod.isLocked();
            if (l && !locked) { dec.reset(); locks++; }
            locked = l;
            if (nfr && l) {
                deliv++;
                uint32_t a0 = m17StreamStats.accepted;
                if (dec.decodeFrame(demod.getSoftFrame()) == FrameType::STREAM) stream++;
                if (m17StreamStats.accepted != a0) acc++;
            }
        }
    }
    demod.terminate();
    printf("%s: %.2f s, %ld locks, %ld frames delivered, %ld stream, %ld accepted, %ld demod spikes\n",
           path, raw.size() / 48000.0, locks, deliv, stream, acc, spikes);
    return 0;
}

int main(int argc, char **argv)
{
    if ((argc == 3) && (std::string(argv[1]) == "replay"))
        return replay(argv[2]);
    const int TX = 10, FRAMES = 50;
    const double FS = 48000.0, IF = 8000.0;
    auto frames = M17Signal::voiceFrames(FRAMES);
    std::map<uint16_t, std::vector<uint8_t>> ref;
    {
        FrameDecoder d;
        for (auto &f : frames)
            if (d.decodeFrame(clean(f)) == FrameType::STREAM) {
                StreamFrame sf = d.getStreamFrame();
                ref[sf.getFrameNumber() & 0x7FFF] = std::vector<uint8_t>(sf.data(), sf.data() + 16);
            }
    }
    std::vector<int8_t> syms(240, 0);       // 50 ms of carrier first
    auto s = M17Signal::m17Symbols(frames);
    syms.insert(syms.end(), s.begin(), s.end());
    syms.insert(syms.end(), 240, 0);

    // Instantaneous frequency at 48 kHz: symbols every 10 samples, RRC, 800 Hz a unit
    std::vector<double> up(syms.size() * 10, 0.0);
    for (size_t i = 0; i < syms.size(); i++) up[i * 10] = syms[i];
    double tapSum = 0; for (float t : rrc_taps_48k) tapSum += t;
    const double scale = 800.0 / (tapSum / 10.0);
    std::vector<double> freq(up.size(), 0.0);
    for (size_t n = 0; n < up.size(); n++) {
        double acc = 0;
        for (size_t k = 0; k < rrc_taps_48k.size() && k <= n; k++) acc += rrc_taps_48k[k] * up[n - k];
        freq[n] = acc * scale;
    }
    // Channel filter for the noise: complex low-pass, 9 kHz, windowed sinc
    const int NT = 63; std::vector<double> lp(NT);
    double lpSum = 0;
    for (int k = 0; k < NT; k++) {
        double x = k - (NT - 1) / 2.0, fc = 9000.0 / FS;
        double v = (x == 0) ? 2 * fc : std::sin(2 * M_PI * fc * x) / (M_PI * x);
        v *= 0.54 - 0.46 * std::cos(2 * M_PI * k / (NT - 1));
        lp[k] = v; lpSum += v * v;
    }

    printf("CNR dB | sent | delivered | stream | accepted right | wrong | demod spikes\n");
    for (int ai = 1; ai < argc; ai++) {
        const double cnr = atof(argv[ai]);
        // Noise rms in the 18 kHz channel for this CNR (signal power 1)
        const double nrms = std::sqrt(std::pow(10.0, -cnr / 10.0));
        const double sig = nrms / std::sqrt(lpSum);   // white noise per component, before the filter
        std::mt19937 rng(99);
        std::normal_distribution<double> g(0.0, sig / std::sqrt(2.0));
        long sent = 0, deliv = 0, stream = 0, right = 0, wrong = 0, spikes = 0;
        for (int t = 0; t < TX; t++) {
            ifDemodReset();
            Demodulator demod; demod.init();
            FrameDecoder dec;
            std::vector<std::complex<double>> nbuf(NT, 0.0);
            double phase = 0;
            std::vector<uint16_t> in(480); std::vector<int16_t> out(240);
            size_t pos = 0; bool locked = false;
            for (size_t n = 0; n < freq.size(); n++) {
                phase += 2 * M_PI * freq[n] / FS;
                std::complex<double> w(g(rng), g(rng));
                nbuf[n % NT] = w;
                std::complex<double> nf = 0;
                for (int k = 0; k < NT; k++) nf += lp[k] * nbuf[(n + NT - k) % NT];
                std::complex<double> z = std::polar(1.0, phase) + nf;
                double x = std::real(z * std::polar(1.0, 2 * M_PI * IF * n / FS));
                int v = (int)std::lround(2048 + 600.0 * x);
                in[pos++] = (uint16_t)std::max(0, std::min(4095, v));
                if (pos == in.size()) {
                    pos = 0;
                    ifDemod(in.data(), out.data(), out.size());
                    for (int16_t y : out) {
                        if (std::abs(y) > 20000) spikes++;
                        bool nfr = demod.sample(y);
                        bool l = demod.isLocked();
                        if (l && !locked) dec.reset();
                        locked = l;
                        if (nfr && l) {
                            deliv++;
                            uint32_t a0 = m17StreamStats.accepted;
                            if (dec.decodeFrame(demod.getSoftFrame()) == FrameType::STREAM) {
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
                }
            }
            demod.terminate();
            sent += FRAMES;
        }
        printf("%6.1f | %4ld | %9ld | %6ld | %6ld (%5.1f %%) | %5ld | %ld\n", cnr, sent, deliv, stream,
               right, 100.0 * right / sent, wrong, spikes);
        fflush(stdout);
    }
    return 0;
}
