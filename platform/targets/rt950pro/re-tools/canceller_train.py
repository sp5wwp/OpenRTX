#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Measure the receive canceller coefficients of the RT-950 Pro (canceller.c)
# on the radio, through the OpenRTX monitor, silently (amplifier power off).
#
#   canceller_train.py [--port /dev/ttyUSB0] [--elf zephyr.elf] [--write]
#
# The radio must run the firmware built as <elf>, in M17 mode ("mode m17"),
# with no signal on the frequency: the receiver noise is what W is designed
# against.
#
# 1. Model (A, B): the DAC outputs white noise, both receivers muted, and the
#    audio node is recorded. A 512-tap FIR is fitted to the node response,
#    then the 6+8 coefficient IIR (Steiglitz-McBride) used by the firmware.
# 2. Filter (W): the receiver noise is recorded with the new model loaded,
#    and the 32-tap filter minimising the noise left on the node is computed.
# 3. Check: W is loaded, the canceller enabled, and the node noise measured.
#
# Prints the C initialisers for canceller.c; --write puts them there.

import os
import re
import struct
import subprocess
import sys
import time

import numpy as np
from scipy.signal import butter, lfilter

from monitor import Monitor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..', '..', '..')
CANCELLER_C = os.path.join(HERE, '..', 'canceller.c')

NA, NB, TAPS = 6, 8, 32
MAX_POLE = 0.99                 # models closer to instability are rejected
POLE_LIMIT = 0.98               # fitted poles are pulled in to this radius
FIR_LEN = 512
SKIP = 600                      # samples dropped at the start of each record
GPIO = {'B': 0x40010C00, 'C': 0x40011000, 'E': 0x40011800}
TG_NOISE, NOISE_AMP = 3, 0x300
MUTE_AF = 0x6042                # BK4829 REG_47: AF output muted


class Radio:
    def __init__(self, port, elf):
        self.m = Monitor(port)
        nm = subprocess.run(['arm-none-eabi-nm', elf], capture_output=True,
                            text=True, check=True).stdout
        self.sym = {t[2]: int(t[0], 16) for t in
                    (l.split() for l in nm.splitlines()) if len(t) == 3}

    def cmd(self, c):
        return self.m.cmd(c)

    def pin(self, name, level):
        port, bit = GPIO[name[1]], int(name[2:])
        self.cmd(f'ww {port + (0x10 if level else 0x14):x} {1 << bit:x}')

    def odt(self, port):
        return int(self.cmd(f'rw {GPIO[port] + 0x0C:x}')[0].split()[1], 16)

    def record(self, n=4096):
        a, d, cnt = (int(x, 16) for x in self.cmd(f'ar 2 {n:x} 1')[0].split())
        v = np.array(struct.unpack(f'<{cnt}H', self.m.mem(a, cnt * 2)), float)
        x = np.array(struct.unpack(f'<{cnt}H', self.m.mem(d, cnt * 2)), float)
        return v, x - 2048

    def load(self, name, values):
        for i, val in enumerate(values):
            word = struct.unpack('<I', struct.pack('<f', float(val)))[0]
            self.cmd(f'ww {self.sym[name] + 4 * i:x} {word:x}')

    def read(self, name, n):
        words = [int(l.split()[1], 16)
                 for l in self.cmd(f'rw {self.sym[name]:x} {n:x}')]
        return np.array([struct.unpack('<f', struct.pack('<I', w))[0]
                         for w in words])

    def clear_state(self):
        for name, n in (('yBuf', 2 * NA), ('sBuf', 2 * TAPS)):
            for i in range(n):
                self.cmd(f'ww {self.sym[name] + 4 * i:x} 0')


def lags(x, first, count):
    return np.stack([np.concatenate([np.zeros(k), x[:len(x) - k]])
                     for k in range(first, first + count)], 1)


def fit_model(recs):
    """FIR of the node response to the DAC, then the IIR approximating it."""
    A_, Y_ = [], []
    for v, x in recs:
        A_.append(lags(x, 1, FIR_LEN)[FIR_LEN:])
        Y_.append((v - v.mean())[FIR_LEN:])
    A_, Y_ = np.vstack(A_), np.concatenate(Y_)
    h = np.linalg.lstsq(A_, Y_, rcond=None)[0]
    fir_err = 10 * np.log10(np.var(Y_ - A_ @ h) / np.var(Y_))

    # The IIR is fitted above 100 Hz: the canceller works on the voice band,
    # and fitting the slow tail of the response put a pole next to 1
    hp = butter(2, 100, 'highpass', fs=48000, output='ba')
    xs = [lfilter(*hp, x) for _, x in recs]
    targets = [lfilter(np.concatenate([[0], h]), [1], x) for x in xs]
    a = np.zeros(NA)
    for _ in range(15):
        rows, tg = [], []
        for x, y in zip(xs, targets):
            den = np.concatenate([[1], a])
            xf, yf = lfilter([1], den, x), lfilter([1], den, y)
            rows.append(np.hstack([lags(xf, 1, NB), -lags(yf, 1, NA)])[FIR_LEN:])
            tg.append(yf[FIR_LEN:])
        th = np.linalg.lstsq(np.vstack(rows), np.concatenate(tg), rcond=None)[0]
        b, a = th[:NB], th[NB:]

    # Stability margin: poles pulled in to POLE_LIMIT, then B fitted again
    # with the poles fixed (linear least squares)
    roots = np.roots(np.concatenate([[1], a]))
    big = np.abs(roots) > POLE_LIMIT
    if big.any():
        roots[big] *= POLE_LIMIT / np.abs(roots[big])
        a = np.real(np.poly(roots))[1:]
        rows, tg = [], []
        for x, y in zip(xs, targets):
            xf = lfilter([1], np.concatenate([[1], a]), x)
            rows.append(lags(xf, 1, NB)[FIR_LEN:])
            tg.append(y[FIR_LEN:])
        b = np.linalg.lstsq(np.vstack(rows), np.concatenate(tg), rcond=None)[0]

    errs = []
    for v, x in recs:
        y = lfilter(np.concatenate([[0], b]), np.concatenate([[1], a]), x)
        vv = v - v.mean()
        errs.append(np.var(vv[FIR_LEN:] - y[FIR_LEN:]) / np.var(vv[FIR_LEN:]))
    poles = np.abs(np.roots(np.concatenate([[1], a]))).max()
    return h, a, b, fir_err, 10 * np.log10(np.mean(errs)), poles


def design_filter(h, noise, lam=1e-3):
    """W minimising the receiver noise left on the node, through h."""
    C, Y = [], []
    for s in noise:
        s = s - s.mean()
        cols = [np.convolve(np.concatenate([[0.0], c]), h)[:len(s)]
                for c in lags(s, 0, TAPS).T]
        C.append(np.stack(cols, 1)[SKIP:])
        Y.append(s[SKIP:])
    C, Y = np.vstack(C), np.concatenate(Y)
    R = C.T @ C
    W = np.linalg.solve(R + lam * np.eye(TAPS) * np.trace(R) / TAPS, -C.T @ Y)
    return W, 10 * np.log10(Y.var() / (Y + C @ W).var())


def simulate(h, a, b, W, noise):
    """Closed loop of the firmware: node = noise + h*x, s = node - IIR(x),
    x = W*s. Returns the node noise change in dB (inf if it diverges)."""
    gain = []
    for r in noise:
        r = r - r.mean()
        x = np.zeros(len(r)); s = np.zeros(len(r)); node = np.zeros(len(r))
        y = np.zeros(len(r))
        for n in range(len(r)):
            past = x[max(0, n - FIR_LEN):n][::-1]
            node[n] = r[n] + np.dot(h[:len(past)], past)
            px = x[max(0, n - NB):n][::-1]
            py = y[max(0, n - NA):n][::-1]
            y[n] = np.dot(b[:len(px)], px) - np.dot(a[:len(py)], py)
            s[n] = node[n] - y[n]
            ps = s[max(0, n - TAPS + 1):n + 1][::-1]
            x[n] = np.dot(W[:len(ps)], ps)
            if abs(x[n]) > 1e5:
                return float('inf')
        gain.append(node[SKIP:].std() / r[SKIP:].std())
    return 20 * np.log10(np.mean(gain))


def c_array(name, size, values):
    body = ''.join(f'    {v:.7g}f,\n' for v in values)
    return f'float {name}[{size}] = {{\n{body}}};'


def write_canceller(a, b, W):
    src = open(CANCELLER_C).read()
    for name, size, vals in (('canceller_A', 'CANCELLER_NA', a),
                             ('canceller_B', 'CANCELLER_NB', b),
                             ('canceller_W', 'CANCELLER_TAPS', W)):
        src = re.sub(rf'float {name}\[{size}\] = \{{.*?\}};',
                     lambda _: c_array(name, size, vals), src, flags=re.S)
    open(CANCELLER_C, 'w').write(src)


def main():
    args = sys.argv[1:]
    port, elf, write = '/dev/ttyUSB0', os.path.join(
        ROOT, 'build_rt950pro', 'zephyr', 'zephyr.elf'), False
    while args:
        if args[0] == '--port':
            port, args = args[1], args[2:]
        elif args[0] == '--elf':
            elf, args = args[1], args[2:]
        elif args[0] == '--write':
            write, args = True, args[1:]
        else:
            sys.exit(__doc__ if __doc__ else 'bad argument')

    r = Radio(port, elf)
    old = {name: r.read(name, n) for name, n in
           (('canceller_A', NA), ('canceller_B', NB), ('canceller_W', TAPS))}
    accepted = False
    pb8 = (r.odt('B') >> 8) & 1
    pc12 = (r.odt('C') >> 12) & 1
    af = {c: r.cmd(f'xr {c} 47')[0] for c in 'vu'}
    try:
        # Silent: amplifier powered off, DAC to the audio node
        r.pin('PE4', 0)
        r.pin('PB8', 0)
        r.pin('PC12', 0)
        r.cmd('cx 0')

        print('model: DAC noise, receivers muted')
        for c in 'vu':
            r.cmd(f'xw {c} 47 {MUTE_AF:x}')
        r.cmd(f'tg {TG_NOISE:x} 0 {NOISE_AMP:x}')
        time.sleep(0.1)
        recs = [r.record() for _ in range(3)]
        r.cmd('tg 0')
        for c in 'vu':
            r.cmd(f'xw {c} 47 {af[c]}')

        h, a, b, fir_err, iir_err, poles = fit_model(recs)
        print(f'  FIR error {fir_err:.1f} dB, IIR error {iir_err:.1f} dB, '
              f'largest pole {poles:.3f}')
        if poles >= MAX_POLE:
            sys.exit(f'model too close to instability (pole {poles:.3f}), '
                     'nothing loaded')

        print('filter: receiver noise')
        r.load('canceller_A', a)
        r.load('canceller_B', b)
        r.load('canceller_W', np.zeros(TAPS))
        r.clear_state()
        time.sleep(0.2)
        noise = [r.record()[0] for _ in range(3)]
        W, predicted = design_filter(h, noise[:2])
        print(f'  predicted reduction {predicted:.1f} dB')
        sim = simulate(h, a, b, W, [n[:1500] for n in noise[2:]])
        print(f'  closed loop simulation: {sim:.1f} dB')
        if not (sim < -3.0):
            sys.exit('the closed loop does not reduce the noise, '
                     'nothing kept')

        r.load('canceller_W', W)
        r.cmd('cx 1')
        time.sleep(0.3)
        v, x = r.record()
        r.cmd('cx 0')
        base = np.mean([n.std() for n in noise])
        measured = 20 * np.log10(v.std() / base)
        print(f'  measured: node noise {base:.1f} -> {v.std():.1f} counts '
              f'({measured:.1f} dB), DAC correction peak {np.abs(x).max():.0f}')
        accepted = measured < -6.0
    finally:
        if not accepted:
            # Never leave unproven coefficients running
            for name, vals in old.items():
                r.load(name, vals)
            r.clear_state()
            print('previous coefficients restored')
        r.cmd('tg 0')
        r.cmd('cx 1')
        r.pin('PB8', pb8)
        r.pin('PC12', pc12)
        r.pin('PE4', 1)

    if not accepted:
        sys.exit('no improvement, nothing written')

    print()
    for name, size, vals in (('canceller_A', 'CANCELLER_NA', a),
                             ('canceller_B', 'CANCELLER_NB', b),
                             ('canceller_W', 'CANCELLER_TAPS', W)):
        print(c_array(name, size, vals))
    if write:
        write_canceller(a, b, W)
        print(f'written to {os.path.normpath(CANCELLER_C)}')


if __name__ == '__main__':
    main()
