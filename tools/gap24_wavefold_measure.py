#!/usr/bin/env python3
import numpy as np
from gap24_wavefold_helpers import sat_wavefold, upsamp, downsamp

def measure(fs, f, drive, asym, os, dur=3.0):
    n = int(fs * dur)
    sig = 0.9 * np.sin(2 * np.pi * f * np.arange(n) / fs)
    y = downsamp(sat_wavefold(upsamp(sig, os), drive, asym), os)[:n]
    win = np.hanning(n) * np.sqrt(8.0 / 3.0)  # hanning amplitude correction
    spec = np.abs(np.fft.rfft(y * win))
    freq = np.fft.rfftfreq(n, 1.0 / fs)
    nyq = fs / 2.0
    fund = spec[int(round(f * n / fs))]
    fund_db = 20 * np.log10(max(fund, 1e-10))

    # Classify each in-band bin: legitimate harmonic of f, or alias/leakage.
    # Use a small exclusion zone around each harmonic for the fundamental's skirt.
    harm_set = set()
    maxh = int(nyq / f)
    for h in range(0, maxh + 1):
        hf = h * f
        idx = int(round(hf * n / fs))
        harm_set.add(idx)

    non_harm_sq = 0.0
    strongest = (-1.0, -180.0)  # (hz, dbc)
    used = set()
    for k in range(len(freq)):
        if freq[k] >= nyq * 0.99:
            continue
        db = 20 * np.log10(spec[k] + 1e-10)
        if db < fund_db - 120:
            continue
        if k in harm_set:
            continue
        # reject 3-bin leakage skirt around true harmonic bins
        nn = min([abs(k - hk) for hk in harm_set] or [999])
        if nn <= 1 and db < fund_db - 40:
            continue
        non_harm_sq += spec[k] ** 2
        if db - fund_db > strongest[1]:
            strongest = (freq[k], db - fund_db)

    total_alias_db = (10 * np.log10(non_harm_sq) - fund_db) if non_harm_sq > 0 else -180.0
    n_harm = sum(1 for h in range(1, maxh + 1)
                 if 20 * np.log10(spec[int(round(h * f * n / fs))] + 1e-10) >= fund_db - 90)
    return {
        'os': os, 'f': f, 'fund_db': fund_db,
        'strongest_alias_dbc': strongest[1],
        'strongest_alias_hz': strongest[0],
        'total_alias_db': total_alias_db,
        'n_harm': n_harm,
    }
