#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Measure an M17 transmission recorded with an SDR.
#
#   m17_analyze.py <capture> [--fs 960000] [--offset 100000] [--eye eye.png]
#                  [--sym out.sym]
#
# <capture> is complex64 IQ (.cf32, as written by a B210 with UHD) or
# unsigned 8-bit IQ (.cu8, rtl_sdr), with the signal --offset Hz above the
# tuned frequency. The carrier is found by its level; then:
# - FM demodulation at 48 kHz, RRC matched filter (alpha 0.5, unity DC gain,
#   so that symbol samples read in Hz of deviation);
# - per 0.5 s segment, the best fixed symbol timing (1/80 symbol), the
#   decisions and the modulation error ratio (MER);
# - the outer symbol levels in Hz (+/-2400 nominal);
# - the symbol timing in 10 ms windows (wander and trend);
# - optionally an eye diagram, and the symbols at 4800/s for gr-m17
#   (float32, normalised to +/-3).

import argparse
import sys

import numpy as np
from scipy.signal import firwin, lfilter, resample_poly

RATE = 48000
SPS = 10            # samples per symbol at 48 kHz
UP = 8              # timing resolution: 1/80 symbol


def rrc(alpha=0.5, span=8, sps=SPS):
    t = np.arange(-span * sps / 2, span * sps / 2 + 1) / sps
    h = np.zeros(len(t))
    for i, x in enumerate(t):
        if x == 0:
            h[i] = 1 - alpha + 4 * alpha / np.pi
        elif abs(abs(x) - 1 / (4 * alpha)) < 1e-9:
            h[i] = alpha / np.sqrt(2) * ((1 + 2 / np.pi) * np.sin(np.pi / (4 * alpha))
                                         + (1 - 2 / np.pi) * np.cos(np.pi / (4 * alpha)))
        else:
            h[i] = (np.sin(np.pi * x * (1 - alpha)) + 4 * alpha * x * np.cos(np.pi * x * (1 + alpha))) \
                / (np.pi * x * (1 - (4 * alpha * x) ** 2))
    return h / h.sum()


def load(path):
    if path.endswith('.cu8'):
        raw = np.fromfile(path, np.uint8).astype(np.float32)
        return (raw[0::2] - 127.4) + 1j * (raw[1::2] - 127.4)
    return np.fromfile(path, np.complex64)


def demodulate(iq, fs, offset):
    """FM demodulated signal at 48 kHz, in Hz, and the envelope."""
    dec = int(round(fs / RATE))
    n = np.arange(len(iq))
    bb = iq * np.exp(-2j * np.pi * offset * n / fs)
    bb = lfilter(firwin(201, 12500, fs=fs), 1, bb)[::dec]
    f = np.angle(bb[1:] * np.conj(bb[:-1])) * RATE / (2 * np.pi)
    return f, np.abs(bb[1:])


def carrier(f, amp):
    on = np.where(amp > 0.3 * np.percentile(amp, 99.5))[0]
    if len(on) < RATE:
        sys.exit('no carrier found')
    return on[0], on[-1]


def best_phase(yu):
    return max(range(SPS * UP), key=lambda p: np.mean(yu[p::SPS * UP] ** 2)
               / (np.var(np.abs(yu[p::SPS * UP])) + 1e-9))


def decide(s):
    g = 3 / np.median(np.abs(s)[np.abs(s) > np.percentile(np.abs(s), 50)])
    return np.clip(2 * np.round((s * g + 3) / 2) - 3, -3, 3), g


def segments(y, seg_s=0.5):
    """Per segment: MER (dB), outer levels (Hz), symbols at the best timing."""
    out = []
    n = int(seg_s * RATE)
    for i0 in range(0, len(y) - n, n):
        yu = resample_poly(y[i0:i0 + n], UP, 1)
        p = best_phase(yu)
        s = yu[p::SPS * UP][4:-4]
        d, g = decide(s)
        a = np.stack([d, np.ones_like(d)], 1)
        c, *_ = np.linalg.lstsq(a, s * g, rcond=None)
        e = s * g - a @ c
        mer = 10 * np.log10(np.mean((a @ c - c[1]) ** 2) / np.mean(e ** 2))
        out.append((mer, np.mean(s[d == 3]), np.mean(s[d == -3])))
    return out


def timing_track(y, win=48, symbols=None):
    """Best symbol timing per window of win symbols, 1/80 symbol units; the
    symbols at that timing, contiguous, appended to the symbols list."""
    yu = resample_poly(y, UP, 1)
    p0 = best_phase(yu[: RATE * UP])
    s0 = yu[p0::SPS * UP]
    _, g = decide(s0)
    track = []
    for w0 in range(1, len(s0) - win - 1, win):
        best = None
        for dt in range(-30, 31):
            idx = p0 + dt + SPS * UP * np.arange(w0, w0 + win)
            if idx[-1] >= len(yu) or idx[0] < 0:
                continue
            s = yu[idx] * g
            d = np.clip(2 * np.round((s + 3) / 2) - 3, -3, 3)
            err = np.mean((s - d) ** 2)
            if best is None or err < best[0]:
                best = (err, dt, s)
        if best:
            track.append(best[1])
            if symbols is not None:
                symbols.append(best[2])
    return np.array(track)


def eye(y, path, title):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    yu = resample_poly(y[: RATE], UP, 1)
    p = best_phase(yu) + SPS * UP // 2
    fig, ax = plt.subplots(figsize=(7, 5))
    for k in range((len(yu) - p - 2 * SPS * UP) // (SPS * UP)):
        seg = yu[p + SPS * UP * k: p + SPS * UP * (k + 2)]
        ax.plot(np.arange(len(seg)) / (SPS * UP) - 0.5, seg, color='#1f5fa8',
                alpha=0.04, lw=0.8)
    for lv in (-2400, -800, 800, 2400):
        ax.axhline(lv, color='#c0392b', lw=0.6, ls='--')
    ax.set_xlim(-0.5, 1.5)
    ax.set_xlabel('time (symbols)')
    ax.set_ylabel('deviation after RRC matched filter (Hz)')
    ax.set_title(title)
    ax.grid(alpha=0.2)
    fig.tight_layout()
    fig.savefig(path, dpi=110)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('capture')
    ap.add_argument('--fs', type=float, default=960000)
    ap.add_argument('--offset', type=float, default=100000)
    ap.add_argument('--skip', type=float, default=0.4,
                    help='seconds skipped after key-up (preamble, LSF)')
    ap.add_argument('--eye')
    ap.add_argument('--sym')
    a = ap.parse_args()

    f, amp = demodulate(load(a.capture), a.fs, a.offset)
    i0, i1 = carrier(f, amp)
    x = f[i0:i1] - np.median(f[i0:i1])
    print(f'carrier {i0 / RATE:.2f}-{i1 / RATE:.2f} s ({(i1 - i0) / RATE:.2f} s), '
          f'offset {np.median(f[i0:i1]):+.0f} Hz')

    y = np.convolve(x, rrc(), 'same')
    body = y[int(a.skip * RATE): len(y) - int(0.1 * RATE)]
    seg = segments(body)
    mer = np.array([q[0] for q in seg])
    print(f'MER median {np.median(mer):.1f} dB (min {mer.min():.1f}, max {mer.max():.1f}); '
          f'per 0.5 s {np.round(mer, 1).tolist()}')
    print(f'outer symbols {np.mean([q[1] for q in seg]):+.0f} / {np.mean([q[2] for q in seg]):+.0f} Hz '
          f'(nominal +/-2400)')

    syms = []
    tr = timing_track(y, symbols=syms) if a.sym else None
    tr = timing_track(body)
    if len(tr) > 2:
        slope = np.polyfit(np.arange(len(tr)), tr, 1)[0]
        print(f'timing wander {tr.std() / (SPS * UP) / 4800 * 1e6:.1f} us rms, trend '
              f'{slope / (SPS * UP) / 48 * 1e6:+.0f} ppm')

    if a.eye:
        eye(body, a.eye, f'{a.capture.rsplit("/", 1)[-1]}: MER {np.median(mer):.1f} dB')
        print('eye diagram:', a.eye)
    if a.sym:
        # The whole transmission, preamble and LSF included, timing tracked
        np.concatenate(syms).astype(np.float32).tofile(a.sym)
        print('symbols:', a.sym)


if __name__ == '__main__':
    main()
