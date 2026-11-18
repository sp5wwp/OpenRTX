#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Approximate the C62 TETRA receive path on an I/Q recording: carrier offset,
white noise at a given Es/N0, a channel filter, an FM discriminator and the
48 kHz audio output, as int16 samples for c62_tetra_host.

The BK4819 filters are not documented: the channel filter is a Butterworth
low-pass of selectable order and bandwidth, and the AF path a low-pass at
48 kHz. Real captures (c62_bbdump.py) remain the reference.

  c62_tetra_sim.py in_250k.wav out.s16 [--cfo 300] [--esn0 20] [--bw 12000]
"""

import argparse
import wave

import numpy as np
from scipy import signal


def read_iq(path, seconds):
    with wave.open(path) as w:
        rate = w.getframerate()
        n = w.getnframes() if seconds is None else int(seconds * rate)
        raw = np.frombuffer(w.readframes(n), dtype="<i2").reshape(-1, 2)
    return (raw[:, 0] + 1j * raw[:, 1]).astype(np.complex64) / 32768, rate


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inp")
    ap.add_argument("out")
    ap.add_argument("--cfo", type=float, default=0, help="Hz added")
    ap.add_argument("--esn0", type=float, default=None, help="dB, in 18 kHz")
    ap.add_argument("--bw", type=float, default=12000, help="filter, Hz/side")
    ap.add_argument("--order", type=int, default=6)
    ap.add_argument("--af", type=float, default=20000, help="AF LPF, Hz")
    ap.add_argument("--level", type=float, default=0.25,
                    help="full scale per 18 kHz of deviation")
    ap.add_argument("--seconds", type=float, default=None)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()

    x, fs = read_iq(a.inp, a.seconds)
    t = np.arange(len(x)) / fs
    # Recording's own offset is left in; add the requested one
    x = x * np.exp(2j * np.pi * a.cfo * t)
    if a.esn0 is not None:
        # Signal power measured in the TETRA channel (+-12.5 kHz)
        b = signal.firwin(255, 12500, fs=fs)
        p = np.mean(np.abs(signal.lfilter(b, 1, x)) ** 2)
        n0 = p / 18000 / 10 ** (a.esn0 / 10)  # power per Hz
        rng = np.random.default_rng(a.seed)
        sigma = np.sqrt(n0 * fs / 2)
        x = x + sigma * (rng.standard_normal(len(x))
                         + 1j * rng.standard_normal(len(x)))
    sos = signal.butter(a.order, a.bw, fs=fs, output="sos")
    y = signal.sosfilt(sos, x)
    # Discriminator: instantaneous frequency in Hz
    d = np.angle(y[1:] * np.conj(y[:-1])) * fs / (2 * np.pi)
    # Resample to 48 kHz with an AF low-pass
    up, down = 48, int(fs / 1000)
    g = np.gcd(up, down)
    sos = signal.butter(4, a.af, fs=fs, output="sos")
    d = signal.resample_poly(signal.sosfilt(sos, d), up // g, down // g)
    s = np.clip(d / 18000 * a.level * 32768, -32768, 32767)
    s.astype("<i2").tofile(a.out)


if __name__ == "__main__":
    main()
