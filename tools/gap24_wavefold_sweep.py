#!/usr/bin/env python3
"""Drive + frequency sweep for Wavefold aliasing (2x vs 4x)."""
import numpy as np
from gap24_wavefold_measure import measure

fs = 48000.0

def fmt_row(f, drive, os):
    r = measure(fs, f, drive, 0.0, os)
    return (r['strongest_alias_dbc'], r['total_alias_db'], r['n_harm'])

print('ZScorch Wavefold alias study | fs=48kHz, in=0.9 hot sine, asym=0')
print('Columns: strongest-single-alias dBc / total-non-harmonic-alias dBc')
print('=' * 78)

print('\n=== Frequency sweep @ drive=1.0 ===')
print(f'{"f":>7} | {"2x":>22} | {"4x":>22} | gap(2x-4x total)')
for fk in (1000.0, 5000.0, 10000.0, 14000.0, 16000.0, 18000.0, 19000.0, 20000.0):
    r2 = measure(fs, fk, 1.0, 0.0, 2)
    r4 = measure(fs, fk, 1.0, 0.0, 4)
    print(f'{fk/1000:>6.0f}k | '
          f'{r2["strongest_alias_dbc"]:>5.1f}/{r2["total_alias_db"]:>5.1f} | '
          f'{r4["strongest_alias_dbc"]:>5.1f}/{r4["total_alias_db"]:>5.1f} | '
          f'{r2["total_alias_db"] - r4["total_alias_db"]:>+6.1f} dB')

print('\n=== Drive sweep @ f=18 kHz ===')
print(f'{"drive":>6} | {"2x total":>11} | {"4x total":>11} | gap (db)')
for drv in (0.0, 0.2, 0.4, 0.6, 0.8, 1.0):
    r2 = measure(fs, 18000.0, drv, 0.0, 2)
    r4 = measure(fs, 18000.0, drv, 0.0, 4)
    print(f'{drv:>6.1f} | {r2["total_alias_db"]:>10.1f} | '
          f'{r4["total_alias_db"]:>10.1f} | '
          f'{r2["total_alias_db"] - r4["total_alias_db"]:>+6.1f}')
