#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
TETRA pi/4-DQPSK demodulator for FM-discriminator audio, such as the C62
baseband dump (48 kHz, BK4819 AF output in M17 mode).

An FM discriminator outputs the instantaneous frequency. Its integral over
one symbol (1/18000 s) is the symbol's phase change, which is what
pi/4-DQPSK carries: +-pi/4 or +-3pi/4. Discriminator clicks add whole turns
of 2 pi, which disappear when the phase change is wrapped.

Steps:
  1. resample 48 kHz -> 144 kHz (8 samples per symbol)
  2. integrate over one symbol (moving sum of 8 samples)
  3. estimate gain (Hz per LSB) and carrier offset from the samples
  4. symbol timing per block, smoothed, so it follows clock drift
  5. optional decision-directed LMS equalizer on the fractionally spaced
     integrated signal: removes the ISI from the BK4819 filters
  6. decisions -> bits, one per byte (0/1), or soft bits (int8, > 0 means
     '1'), as libtetra's tetra_rx reads them with -t bit / -t soft.
     Spectrum polarity is detected from the training sequences.

The equalizer defaults to a 9-tap symbol-spaced least-squares fit over the
whole file; on the C62 it comes out as a short symmetric filter (about
[0.03 -0.18 1.14 -0.18 0.03]) undoing the BK4819 filters' ISI.

Usage (libtetra: https://github.com/sp5wwp/libtetra):
  c62_tetra_demod.py tetra_burst001.wav -o burst001.soft --soft
  tetra_rx -m tmo -t soft -s speech.ser burst001.soft
"""

import argparse
import sys
import wave

import numpy as np
from scipy import signal

SYMBOL_RATE = 18000
SPS = 8
FS = SYMBOL_RATE * SPS  # 144 kHz

# Dibit for each phase change in units of pi/4: +1, +3, -3, -1
DIBITS = {1: (0, 0), 3: (0, 1), -3: (1, 1), -1: (1, 0)}

# Training sequences (EN 300 392-2 9.4.4.3), from osmo-tetra phy/tetra_burst.c
TRAIN = {
    "n": [1,1,0,1,0,0,0,0,1,1,1,0,1,0,0,1,1,1,0,1,0,0],
    "p": [0,1,1,1,1,0,1,0,0,1,0,0,0,0,1,1,0,1,1,1,1,0],
    "y": [1,1,0,0,0,0,0,1,1,0,0,1,1,1,0,0,1,1,1,0,1,0,0,1,1,1,0,0,0,0,0,1,
          1,0,0,1,1,1],
}


def read_audio(path):
    if path.endswith(".f32"):
        return np.fromfile(path, dtype="<f4").astype(np.float64), 48000
    with wave.open(path) as w:
        if w.getsampwidth() != 2 or w.getnchannels() != 1:
            sys.exit("expected a mono 16-bit WAV")
        rate = w.getframerate()
        x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2")
    return x.astype(np.float64) / 32768.0, rate


def wrap(phi):
    """Wrap to [-pi, pi)."""
    return (phi + np.pi) % (2 * np.pi) - np.pi


def decide(phi):
    """Nearest of +-pi/4, +-3pi/4, in units of pi/4 (odd integers)."""
    q = np.round((wrap(phi) / (np.pi / 4) - 1) / 2) * 2 + 1
    q[q > 3] = -3  # +5pi/4 wraps to -3pi/4
    return q


def estimate_gain_offset(d):
    """
    d: integrated discriminator output at (roughly) symbol instants.
    Returns (gain, offset) so that gain * (d - offset) is the phase change.
    Starts from the moments (random data: mean 0, mean |phi| = pi/2), then
    refines by least squares against the decisions.
    """
    off = np.median(d)
    gain = (np.pi / 2) / np.mean(np.abs(d - off))
    for _ in range(10):
        phi = gain * (d - off)
        q = decide(phi)
        target = q * np.pi / 4
        # Undo 2 pi clicks so the fit sees the unwrapped target
        target = target + 2 * np.pi * np.round((phi - target) / (2 * np.pi))
        a = np.vstack([d, np.ones_like(d)]).T
        sol, *_ = np.linalg.lstsq(a, target, rcond=None)
        gain, off = sol[0], -sol[1] / sol[0]
    return gain, off


def timing(y, gain, off, block):
    """
    Best symbol phase (0..SPS) per block of symbols, as a fractional sample
    offset. y is the integrated signal at SPS samples per symbol.
    """
    nsym = len(y) // SPS - 2
    nblk = max(1, nsym // block)
    best = np.zeros(nblk)
    for b in range(nblk):
        s0 = b * block
        costs = []
        for ph in range(SPS):
            idx = (np.arange(s0, min(s0 + block, nsym)) * SPS + ph)
            phi = gain * (y[idx] - off)
            e = wrap(phi - decide(phi) * np.pi / 4)
            costs.append(np.mean(e ** 2))
        costs = np.array(costs)
        k = int(np.argmin(costs))
        # Parabolic interpolation on the circular cost
        cm, c0, cp = costs[(k - 1) % SPS], costs[k], costs[(k + 1) % SPS]
        den = cm - 2 * c0 + cp
        frac = 0.5 * (cm - cp) / den if den > 0 else 0.0
        best[b] = k + frac
    # The sample clock offset is constant, so the timing drifts linearly.
    # Find that line on the circle (no unwrapping of noisy estimates, which
    # slips whole symbols), then follow the residual with a moving median.
    ang = 2 * np.pi * best / SPS
    idx = np.arange(nblk)
    slopes = np.linspace(-0.5, 0.5, 4001)  # samples per block, +-240 ppm
    score = np.abs(np.exp(1j * (ang[None, :] - 2 * np.pi * slopes[:, None]
                                * idx[None, :] / SPS)).sum(axis=1))
    slope = slopes[np.argmax(score)]
    rot = np.exp(1j * (ang - 2 * np.pi * slope * idx / SPS))
    base = np.angle(rot.sum())
    resid = np.angle(rot * np.exp(-1j * base)) * SPS / (2 * np.pi)
    if nblk >= 9:
        resid = signal.medfilt(resid, 9)
    return base * SPS / (2 * np.pi) + slope * idx + resid


def sample_at(y, positions):
    """Linear interpolation of y at fractional positions."""
    i = np.floor(positions).astype(int)
    f = positions - i
    i = np.clip(i, 0, len(y) - 2)
    return y[i] * (1 - f) + y[i + 1] * f


def lms_equalize(x, centers, gain, off, taps, mu, train_passes=1):
    """
    Decision-directed LMS on the integrated signal. For each symbol, the
    input vector is `taps` samples of x (SPS per symbol) around its centre;
    the initial weights pick the centre sample, i.e. no equalization.
    Returns equalized phase changes.
    """
    half = taps // 2
    w = np.zeros(taps)
    w[half] = 1.0
    xs = gain * (x - off)
    n = len(centers)
    out = np.empty(n)
    base = np.round(centers).astype(int)
    frac = centers - base
    valid = (base - half - 1 >= 0) & (base + half + 2 < len(xs))
    for p in range(train_passes + 1):
        adapt = p < train_passes or True
        for k in range(n):
            if not valid[k]:
                out[k] = 0.0
                continue
            b = base[k]
            seg = xs[b - half:b + half + 1]
            nxt = xs[b - half + 1:b + half + 2]
            v = seg * (1 - frac[k]) + nxt * frac[k] if frac[k] >= 0 else seg
            y = np.dot(w, v)
            q = decide(np.array([y]))[0] * np.pi / 4
            e = wrap(np.array([y - q]))[0]
            if adapt:
                w -= mu * e * v / (np.dot(v, v) + 1e-9)
            out[k] = y
    return out, w


def ls_equalize(d, gain, off, taps=9, iters=3):
    """
    Least-squares T-spaced equalizer on the integrate-and-dump outputs:
    phi_k = sum_i w_i * d[k + i - taps//2] + c, fitted in one batch against
    the decisions (unwrapped by whole turns), then re-decided and refitted.
    """
    half = taps // 2
    x = gain * (d - off)
    n = len(x)
    cols = [np.roll(x, half - i) for i in range(taps)]
    a = np.vstack(cols + [np.ones(n)]).T[half:n - half]
    phi = x.copy()
    for _ in range(iters):
        q = decide(phi) * np.pi / 4
        t = q + 2 * np.pi * np.round((phi - q) / (2 * np.pi))
        w, *_ = np.linalg.lstsq(a, t[half:n - half], rcond=None)
        phi = x.copy()
        phi[half:n - half] = a @ w
    return phi, w


def to_bits(q):
    bits = np.empty(2 * len(q), dtype=np.uint8)
    for k, v in enumerate(q.astype(int)):
        bits[2 * k], bits[2 * k + 1] = DIBITS[v]
    return bits


def to_soft(phi, scale=40.0):
    """
    Soft bits for libtetra (int8, > 0 means '1'). For the TETRA mapping the
    first bit is 1 for a negative phase change, the second for |phi| > pi/2.
    scale is per pi/4 of distance from the decision boundary.
    """
    k = scale / (np.pi / 4)
    soft = np.empty(2 * len(phi))
    soft[0::2] = -phi * k
    soft[1::2] = (np.abs(phi) - np.pi / 2) * k
    return np.clip(np.round(soft), -127, 127).astype(np.int8)


def count_train(bits):
    """Exact training sequence matches, as tetra-rx requires."""
    s = bits.tobytes()
    return {k: s.count(bytes(v)) for k, v in TRAIN.items()}


def demod(x, rate, eq="ls", taps=9, mu=0.002, block=256, verbose=True,
          invert=None):
    if rate != 48000:
        sys.exit("expected 48 kHz audio")
    # 1. 48 kHz -> 144 kHz
    z = signal.resample_poly(x, 3, 1)
    # 2. integrate over one symbol
    y = np.convolve(z, np.ones(SPS), mode="same")
    # 3. coarse gain/offset on a guessed phase, then timing, then refine
    nsym = len(y) // SPS - 2
    gain, off = estimate_gain_offset(y[np.arange(nsym) * SPS])
    tim = timing(y, gain, off, block)
    pos = (np.arange(nsym) * SPS
           + np.interp(np.arange(nsym), np.arange(len(tim)) * block
                       + block / 2, tim))
    d = sample_at(y, pos)
    gain, off = estimate_gain_offset(d)
    tim = timing(y, gain, off, block)
    pos = (np.arange(nsym) * SPS
           + np.interp(np.arange(nsym), np.arange(len(tim)) * block
                       + block / 2, tim))
    pos = pos[(pos > SPS) & (pos < len(y) - 2 * SPS)]
    d = sample_at(y, pos)
    gain, off = estimate_gain_offset(d)
    phi = wrap(gain * (d - off))
    if eq == "lms":
        phi, w = lms_equalize(y, pos, gain, off, taps, mu)
        phi = wrap(phi)
    elif eq == "ls":
        phi, w = ls_equalize(d, gain, off, taps)
        phi = wrap(phi)
        if verbose:
            print("LS equalizer taps:", np.array2string(w[:-1], precision=3))
    q = decide(phi)
    err = wrap(phi - q * np.pi / 4)
    evm = np.sqrt(np.mean(err ** 2)) / (np.pi / 4)

    bits = to_bits(q)
    inv = bits.copy()
    inv[0::2] ^= 1  # spectrum inversion negates the phase changes
    c_norm, c_inv = count_train(bits), count_train(inv)
    if invert is None:
        invert = sum(c_inv.values()) > sum(c_norm.values())
    if invert:
        phi = -phi
        bits = inv
    if verbose:
        # phi = gain * (d - off), d = 8 samples summed, phi = 2 pi f / Rs
        full_scale = gain * SPS * SYMBOL_RATE / (2 * np.pi)
        carrier = gain * off * SYMBOL_RATE / (2 * np.pi)
        clock = (tim[-1] - tim[0]) / (len(tim) * block * SPS) * 1e6
        print("symbols %d, discriminator %.2f kHz at full scale, carrier "
              "offset %+.0f Hz, sample clock %+.0f ppm, phase error %.1f%% "
              "rms of pi/4" % (len(q), full_scale / 1e3, carrier, clock,
                               100 * evm))
        print("training sequences found: normal %s, inverted %s -> %s"
              % (c_norm, c_inv, "inverted" if invert else "normal"))
    return bits, phi


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="48 kHz mono WAV or .f32 from c62_bbdump.py")
    ap.add_argument("-o", "--output", help="output file for tetra_rx")
    ap.add_argument("--soft", action="store_true",
                    help="write soft bits (tetra_rx -t soft), default hard "
                    "bits (tetra_rx -t bit)")
    ap.add_argument("--eq", choices=["ls", "lms", "none"], default="ls",
                    help="ls: batch least-squares, symbol-spaced (default); "
                    "lms: adaptive, 1/8-symbol spaced; none")
    ap.add_argument("--taps", type=int,
                    help="equalizer taps (default 9 for ls, 33 for lms)")
    ap.add_argument("--mu", type=float, default=0.002,
                    help="LMS step size (default 0.002)")
    ap.add_argument("--invert", choices=["auto", "yes", "no"], default="auto")
    ap.add_argument("--phases", help="save the phase changes (float32)")
    args = ap.parse_args()

    x, rate = read_audio(args.input)
    inv = {"auto": None, "yes": True, "no": False}[args.invert]
    taps = args.taps or (9 if args.eq == "ls" else 33)
    bits, phi = demod(x, rate, eq=args.eq, taps=taps, mu=args.mu, invert=inv)
    ext = ".soft" if args.soft else ".bits"
    out = args.output or args.input.rsplit(".", 1)[0] + ext
    (to_soft(phi) if args.soft else bits).tofile(out)
    print("wrote %d %s bits to %s, then: tetra_rx -m tmo -t %s %s"
          % (len(bits), "soft" if args.soft else "hard", out,
             "soft" if args.soft else "bit", out))
    if args.phases:
        phi.astype("<f4").tofile(args.phases)


if __name__ == "__main__":
    main()
