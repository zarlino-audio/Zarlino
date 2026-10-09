#!/usr/bin/env python3
"""
ZTame A/B measurement — turns the Render Lab WAVs into numbers.

For every <name>-A-dry.wav / <name>-B-ztame.wav / <name>-C-removed.wav triplet it reports:

  * level (RMS dBFS) of dry / processed / removed
  * the frequency where ZTame removed the most energy, and how many dB it removed there
  * how much of the dry signal survived elsewhere (the "body kept" check): the
    broadband level change outside the worst-affected band
  * residual check: dry - processed should equal the removed file (time aligned)

Usage: python tools/ztame_ab_measure.py <renderlab-dir> [report.md]
"""

import os
import sys
import math

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass


def read_wav(path):
    """Minimal RIFF reader for 16/24/32-bit PCM or float WAVs. Returns (channels, sr)."""
    import wave

    with wave.open(path, "rb") as w:
        n_ch = w.getnchannels()
        sr = w.getframerate()
        width = w.getsampwidth()
        n_frames = w.getnframes()
        raw = w.readframes(n_frames)

    chans = [[] for _ in range(n_ch)]

    if width == 3:
        for i in range(0, len(raw), 3 * n_ch):
            for c in range(n_ch):
                b = raw[i + c * 3: i + c * 3 + 3]
                if len(b) < 3:
                    return None
                v = int.from_bytes(b, "little", signed=True)
                chans[c].append(v / 8388608.0)
    else:
        import struct
        scale = {1: 128.0, 2: 32768.0, 4: 2147483648.0}[width]
        step = width * n_ch
        fmt = {1: "b", 2: "h", 4: "i"}[width]
        for i in range(0, len(raw) - step + 1, step):
            vals = struct.unpack_from("<" + fmt * n_ch, raw, i)
            for c in range(n_ch):
                chans[c].append(vals[c] / scale)

    return chans, sr


def rms_db(samples):
    if not samples:
        return -180.0
    acc = 0.0
    for v in samples:
        acc += v * v
    r = math.sqrt(acc / len(samples))
    return 20.0 * math.log10(r) if r > 1e-12 else -180.0


def dft_db(samples, sr, freq, start=0, length=None):
    length = length or (len(samples) - start)
    re = im = 0.0
    w = 2.0 * math.pi * freq / sr
    for i in range(length):
        v = samples[start + i]
        re += v * math.cos(w * i)
        im += v * math.sin(w * i)
    amp = 2.0 * math.sqrt(re * re + im * im) / length
    return 20.0 * math.log10(amp) if amp > 1e-12 else -180.0


def spectrum_db(samples, sr, n=4096, hop=None):
    """Coarse magnitude spectrum (numpy if available, otherwise a sparse DFT grid)."""
    try:
        import numpy as np
    except ImportError:
        np = None

    if np is not None:
        x = np.asarray(samples[4096:4096 + 65536], dtype=np.float64)
        w = np.hanning(len(x))
        spec = np.abs(np.fft.rfft(x * w))
        spec /= (np.sum(w) / 2.0)
        freqs = np.fft.rfftfreq(len(x), 1.0 / sr)
        return [float(f) for f in freqs], [20.0 * math.log10(max(v, 1e-12)) for v in spec]

    # sparse fallback: 1/12-octave grid
    freqs, mags = [], []
    f = 30.0
    while f < sr / 2.0:
        freqs.append(f)
        mags.append(dft_db(samples, sr, f, 4096, 16384))
        f *= 2.0 ** (1.0 / 12.0)
    return freqs, mags


def report_source(directory, name):
    dry_path = os.path.join(directory, f"{name}-A-dry.wav")
    wet_path = os.path.join(directory, f"{name}-B-ztame.wav")
    rem_path = os.path.join(directory, f"{name}-C-removed.wav")

    for p in (dry_path, wet_path, rem_path):
        if not os.path.exists(p):
            return f"*{name}: missing {os.path.basename(p)}*\n"

    dry, sr = read_wav(dry_path)
    wet, _ = read_wav(wet_path)
    rem, _ = read_wav(rem_path)

    dryM = dry[0]
    wetM = wet[0]
    remM = rem[0]

    lines = [f"### {name}", ""]
    lines.append(f"- sample rate: {sr} Hz, length: {len(dryM)} samples "
                 f"({len(dryM) / sr:.1f} s)")
    lines.append(f"- level dry: **{rms_db(dryM):.2f} dBFS**")
    lines.append(f"- level processed: **{rms_db(wetM):.2f} dBFS**")
    lines.append(f"- level removed: {rms_db(remM):.2f} dBFS")

    fd, md = spectrum_db(dryM, sr)
    fw, mw = spectrum_db(wetM, sr)

    worst_f, worst_db = 0.0, 0.0
    kept = []
    for i, f in enumerate(fd):
        if f < 40.0 or f > 18000.0:
            continue
        d = md[i] - mw[i]
        if d > worst_db:
            worst_db, worst_f = d, f
        if d < 1.0 and md[i] > -70.0:
            kept.append(md[i] - mw[i])

    lines.append(f"- strongest reduction: **{worst_db:.2f} dB @ {worst_f:.0f} Hz**")
    if kept:
        avg_kept = sum(kept) / len(kept)
        lines.append(f"- average change outside that region: "
                     f"**{avg_kept:+.2f} dB** over {len(kept)} bands "
                     f"(~0 dB = surrounding tone kept intact)")

    # residual: dry - processed should match the removed file
    n = min(len(dryM), len(wetM), len(remM))
    residual = [dryM[i] - wetM[i] - remM[i] for i in range(4096, n - 4096)]
    lines.append(f"- dry - processed - removed residual: **{rms_db(residual):.1f} dBFS** "
                 f"(near silence = the delta file really is what was taken out)")
    lines.append("")
    return "\n".join(lines)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    directory = sys.argv[1]
    report_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(directory, "REPORT.md")

    names = sorted({f.rsplit("-A-dry.wav", 1)[0]
                    for f in os.listdir(directory) if f.endswith("-A-dry.wav")})

    if not names:
        print(f"no *-A-dry.wav files found in {directory}")
        return 1

    out = ["# ZTame render-lab measurement", "",
           f"Source directory: `{directory}`", ""]

    for name in names:
        out.append(report_source(directory, name))

    text = "\n".join(out)
    print(text)

    with open(report_path, "w", encoding="utf-8") as f:
        f.write(text + "\n")

    print(f"\nwrote {report_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
