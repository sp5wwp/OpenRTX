#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Measure the frequency error of a USRP B2xx against GSM base stations.
#
#   b210_gsm_cal.py [--band 900|1800] [--carriers 6] [--seconds 2]
#
# A GSM base station holds its carriers to 0.05ppm. Its broadcast (BCCH)
# carrier sends frequency correction bursts (FCCH), a pure tone 1625/24 kHz
# (67.708kHz) above the carrier, several times a second. The band is scanned
# for the strongest 200kHz channels, each is recorded for a while, and the
# FCCH line found near the carrier plus 67.708kHz. A reference running fast
# by e puts the line e times the carrier frequency below that, the LO and
# the sampling both fast: the offset gives e, which one reference shares
# between receive and transmit and all frequencies. Carriers that are
# not BCCH carriers show no line and are left out; the remaining ones have to
# agree.

import argparse

import numpy as np
import uhd

FS = 1.0e6
FCCH = 1625e3 / 24
BANDS = {'900': (935.2e6, 959.8e6), '1800': (1805.2e6, 1879.8e6)}


def record(u, st, freq, seconds):
    u.set_rx_freq(uhd.types.TuneRequest(freq), 0)
    n = int(FS * seconds)
    buf = np.zeros(n, np.complex64)
    cmd = uhd.types.StreamCMD(uhd.types.StreamMode.num_done)
    cmd.num_samps = n
    cmd.stream_now = False
    cmd.time_spec = u.get_time_now() + uhd.types.TimeSpec(0.05)
    st.issue_stream_cmd(cmd)
    md = uhd.types.RXMetadata()
    got = 0
    chunk = np.zeros(st.get_max_num_samps(), np.complex64)
    while got < n:
        k = st.recv(chunk, md, 1.0)
        if k == 0:
            break
        k = min(k, n - got)
        buf[got:got + k] = chunk[:k]
        got += k
    return buf[:got]


def scan(u, st, band):
    lo, hi = BANDS[band]
    powers = []
    # 1MHz chunks, five 200kHz channels each, measured by FFT
    for centre in np.arange(lo + 0.4e6, hi, 1.0e6):
        x = record(u, st, centre, 0.05)
        X = np.abs(np.fft.fftshift(np.fft.fft(x[:32768] * np.hanning(32768)))) ** 2
        f = np.fft.fftshift(np.fft.fftfreq(32768, 1 / FS))
        for k in range(-2, 3):
            ch = centre + k * 200e3
            if lo - 1 <= ch <= hi + 1:
                sel = np.abs(f - k * 200e3) < 90e3
                powers.append((10 * np.log10(X[sel].sum() + 1e-30), ch))
    return sorted(powers, reverse=True)


def fcch_offset(x, search_hz):
    """
    FCCH line's offset from carrier + 67.708kHz, Hz, and how many 0.4ms
    stretches of a steady tone it rests on. Bursts come a few at a time, so
    the line is a comb in a spectrum: it is measured in time instead, from
    the phase steps in the stretches whose frequency holds still, which only
    a FCCH burst does among GSM's bursts.
    """
    n = np.arange(len(x))
    y = x * np.exp(-2j * np.pi * FCCH * n / FS)
    dec = 25
    y = y[:len(y) // dec * dec].reshape(-1, dec).mean(1)
    fs2 = FS / dec
    d = y[1:] * np.conj(y[:-1])
    W = 16
    c = np.convolve(d, np.ones(W), 'valid')
    u = d / (np.abs(d) + 1e-30)
    coh = np.abs(np.convolve(u, np.ones(W), 'valid')) / W
    amp = np.convolve(np.abs(y[1:]) ** 2, np.ones(W), 'valid') / W
    f = np.angle(c) * fs2 / (2 * np.pi)
    sel = (coh > 0.98) & (np.abs(f) < search_hz) & (amp > 2 * np.median(amp))
    if sel.sum() < 30:
        return 0.0, int(sel.sum())
    # Keep the stretches around the most common frequency, then average
    hist, edges = np.histogram(f[sel], bins=np.arange(-search_hz, search_hz + 50, 50))
    centre = edges[np.argmax(hist)] + 25
    keep = sel & (np.abs(f - centre) < 200)
    return np.angle(np.sum(c[keep])) * fs2 / (2 * np.pi), int(keep.sum())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--band', default='900', choices=BANDS.keys())
    ap.add_argument('--carriers', type=int, default=6)
    ap.add_argument('--seconds', type=float, default=2.0)
    ap.add_argument('--gain', type=float, default=50)
    ap.add_argument('--args', default='type=b200')
    a = ap.parse_args()

    u = uhd.usrp.MultiUSRP(a.args)
    u.set_rx_rate(FS, 0)
    u.set_rx_gain(a.gain, 0)
    u.set_rx_antenna('TX/RX', 0)
    st = u.get_rx_stream(uhd.usrp.StreamArgs('fc32', 'sc16'))

    chans = scan(u, st, a.band)
    print('strongest channels:', ', '.join(f'{c / 1e6:.1f}MHz {p:.0f}dB'
                                           for p, c in chans[:a.carriers * 2]))
    results = []
    for p, ch in chans[:a.carriers * 2]:
        x = record(u, st, ch, a.seconds)
        off, steady = fcch_offset(x, 10e3)
        ppm = -off / ch * 1e6
        ok = steady >= 30
        print(f'{ch / 1e6:8.1f}MHz: FCCH {off:+8.1f}Hz from {steady} steady '
              f'stretches -> {ppm:+.3f}ppm{"" if ok else "  (no FCCH)"}')
        if ok:
            results.append(ppm)
        if len(results) >= a.carriers:
            break
    if not results:
        print('no FCCH found: no GSM BCCH carrier in this band here')
        return
    r = np.array(results)
    e = np.median(r)
    print(f'B2xx reference error: {e:+.3f}ppm (median of {len(r)}, spread '
          f'{r.min():+.3f} to {r.max():+.3f}; positive: it runs fast). A signal '
          f'at f shows up {"below" if e > 0 else "above"} f by {abs(e):.3f}ppm, '
          f'{abs(e) * 433.475:.0f}Hz at 433.475MHz')


if __name__ == '__main__':
    main()
