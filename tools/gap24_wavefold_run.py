#!/usr/bin/env python3
"""ZScorch Wavefold 2x vs 4x oversampling alias study (drive=100%, hot input)."""
from gap24_wavefold_measure import measure

fs = 48000.0
cases = [('1 kHz (benign)', 1000.0), ('10 kHz', 10000.0),
         ('18 kHz near-Nyquist', 18000.0), ('20 kHz near-Nyquist', 20000.0)]
print('ZScorch Wavefold alias study | drive=100%, asym=0, fs=48 kHz, in=0.9 hot sine')
print('=' * 78)
for label, f in cases:
    print(f'--- input {label} ---')
    for os in (2, 4):
        r = measure(fs, f, 1.0, 0.0, os)
        print(f"  OS={os}: fund {r['fund_db']:.1f} dB | strongest in-band alias "
              f"{r['strong_alias']:.1f} dBc | harms>=-90dBc: {r['n_harm']}")
        print("    top aliases (Hz, dBc): "
              + str([(round(a[0]), round(a[1], 1)) for a in r['top_aliases']]))
    print()
