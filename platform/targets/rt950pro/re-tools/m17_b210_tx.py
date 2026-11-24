#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Transmit an M17 stream with a USRP B2xx, to test the receiver of a radio.
#
#   m17_b210_tx.py <symbols> [--freq 433475000] [--gain 50] [--repeat 1]
#
# <symbols> is float32 at 4800/s, values -3, -1, +1, +3, for instance from
# the M17 encoder of gr-m17. The symbols are shaped by a root raised cosine
# (alpha 0.5, unity DC gain: a run of +3 deviates by 2.4 kHz), frequency
# modulated at 48 kHz and resampled to 960 kS/s. The B2xx is tuned 100 kHz
# below the frequency and the signal shifted up digitally, so that its LO
# leakage stays off the channel. Half a second of unmodulated carrier comes
# before the stream.

import argparse

import numpy as np
from scipy.signal import resample_poly
import uhd

from m17_analyze import rrc

RATE = 48000
FS = 960000
OFFSET = 100000
DEV_PER_UNIT = 800.0  # Hz of deviation per symbol unit: +/-3 is +/-2.4 kHz


def baseband(sym):
    up = np.zeros(len(sym) * 10)
    up[::10] = sym
    lead = np.zeros(RATE // 2)
    f = np.concatenate([lead, np.convolve(up, rrc() * 10, 'same'), lead]) * DEV_PER_UNIT
    phase = 2 * np.pi * np.cumsum(f) / RATE
    bb = resample_poly(np.exp(1j * phase), FS // RATE, 1)
    n = np.arange(len(bb))
    return (0.7 * bb * np.exp(2j * np.pi * OFFSET * n / FS)).astype(np.complex64)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('symbols')
    ap.add_argument('--freq', type=float, default=433475000)
    ap.add_argument('--gain', type=float, default=50)
    ap.add_argument('--repeat', type=int, default=1)
    ap.add_argument('--args', default='type=b200')
    a = ap.parse_args()

    x = baseband(np.fromfile(a.symbols, np.float32))
    u = uhd.usrp.MultiUSRP(a.args)
    u.set_tx_rate(FS, 0)
    u.set_tx_freq(uhd.types.TuneRequest(a.freq - OFFSET), 0)
    u.set_tx_gain(a.gain, 0)
    u.set_tx_antenna('TX/RX', 0)
    st = u.get_tx_stream(uhd.usrp.StreamArgs('fc32', 'sc16'))
    md = uhd.types.TXMetadata()
    for _ in range(a.repeat):
        md.start_of_burst = True
        md.end_of_burst = False
        sent = 0
        step = st.get_max_num_samps() * 8
        while sent < len(x):
            chunk = x[sent:sent + step]
            md.end_of_burst = (sent + len(chunk)) >= len(x)
            sent += st.send(chunk, md)
            md.start_of_burst = False
    print(f'sent {len(x) / FS:.1f} s x {a.repeat} at {a.freq / 1e6:.4f} MHz, gain {a.gain} dB')


if __name__ == '__main__':
    main()
