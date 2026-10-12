#!/usr/bin/env python3
"""
Zarlino Audio - canonical reference preparation (one-time / re-runnable).

Reads a sources spec (plugins/<P>/references/sources.json), extracts the
byte-exact segment used from each LOCAL source file, writes the committed
reference WAVs next to the spec, and materialises references.json with sha256
hashes and full provenance (original file hash, trim range, rights note).

The committed references are the ONLY canonical inputs; CI consumes them and
never touches the local originals.

Usage:
    python tools/release/prepare-references.py <sources.json>

Exit 0 ok - 1 error. No third-party dependencies (stdlib wave/hashlib only).
ASCII-only (Windows PowerShell 5.1 misparses non-ASCII without a BOM).
"""

import hashlib
import json
import os
import shutil
import struct
import sys
import wave

EPOCH = None  # set from the input file order; no timestamps inside the WAVs


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def flac_streaminfo(path):
    """Parse the STREAMINFO block of a FLAC file.

    Returns (sample_rate, channels, bit_depth, total_samples) or None.
    FLAC inputs are copied BYTE-EXACT (no re-encode, no trim): lossless and
    hash-stable - the strongest possible provenance for a reference.
    """
    with open(path, "rb") as f:
        head = f.read(4)
        if head != b"fLaC":
            return None
        block = f.read(4 + 34)
    if len(block) < 38:
        return None
    body = block[4:38]
    rate = (body[10] << 12) | (body[11] << 4) | (body[12] >> 4)
    channels = ((body[12] >> 1) & 0x07) + 1
    bit_depth = (((body[12] & 0x01) << 4) | (body[13] >> 4)) + 1
    total = ((body[13] & 0x0F) << 32) | (body[14] << 24) | (body[15] << 16) | (body[16] << 8) | body[17]
    if rate <= 0 or channels <= 0 or bit_depth <= 0:
        return None
    return rate, channels, bit_depth, total


def main():
    if len(sys.argv) != 2:
        print("usage: prepare-references.py <sources.json>")
        return 2

    spec_path = os.path.abspath(sys.argv[1])
    if not os.path.isfile(spec_path):
        print("spec not found: " + spec_path)
        return 1

    with open(spec_path, "r", encoding="utf-8") as f:
        spec = json.load(f)

    out_dir = os.path.dirname(spec_path)
    refs = []
    failures = []

    for src in spec.get("sources", []):
        sid = src.get("id", "?")
        path = src.get("path", "")
        if not os.path.isfile(path):
            failures.append(sid + ": source missing: " + path)
            continue

        try:
            with open(path, "rb") as probe:
                magic = probe.read(4)

            out_path = os.path.join(out_dir, src["file"])
            start_sec = float(src.get("startSec") or 0)
            seconds = src.get("seconds")

            if magic == b"fLaC":
                # Lossless passthrough: byte-exact copy, no trim (FLAC segments
                # cannot be cut without re-encoding, and re-encoding would
                # change the evidence identity of the source bytes).
                if start_sec != 0 or seconds is not None:
                    failures.append(sid + ": FLAC sources are copied whole - startSec/seconds must be 0/null")
                    continue
                info = flac_streaminfo(path)
                if info is None:
                    failures.append(sid + ": cannot parse FLAC STREAMINFO")
                    continue
                framerate, channels, bit_depth, total = info
                shutil.copyfile(path, out_path)
                length = total
                duration = total / float(framerate)
                entry = {
                    "id": sid,
                    "file": src["file"],
                    "container": "flac",
                    "sha256": sha256_file(out_path),
                    "bytes": os.path.getsize(out_path),
                    "sampleRate": framerate,
                    "channels": channels,
                    "bitDepth": bit_depth,
                    "frames": total,
                    "durationSec": round(duration, 6),
                    "kind": src.get("kind", "oneshot"),
                    "title": src.get("title", ""),
                    "origin": src.get("origin", ""),
                    "rights": src.get("rights", ""),
                    "trim": None,
                    "source": {
                        "file": os.path.basename(path.replace("\\", "/")),
                        "sha256": sha256_file(path),
                        "bytes": os.path.getsize(path),
                    },
                }
                refs.append(entry)
                print("ok  %-26s flac %d ch %d bit %d Hz  %.2fs  (byte-exact copy)  -> %s"
                      % (sid, channels, bit_depth, framerate, duration, src["file"]))
                continue

            if magic != b"RIFF":
                failures.append(sid + ": not RIFF/WAV or FLAC (magic " + magic.hex() + ")")
                continue

            with wave.open(path, "rb") as w:
                channels = w.getnchannels()
                sampwidth = w.getsampwidth()
                framerate = w.getframerate()
                total = w.getnframes()

                start = int(round(start_sec * framerate))
                if seconds is None:
                    length = total - start
                else:
                    length = int(round(float(seconds) * framerate))
                    length = min(length, total - start)

                if start < 0 or length <= 0 or start + length > total:
                    failures.append(
                        sid + ": trim out of range (start=%d len=%d total=%d)" % (start, length, total)
                    )
                    continue

                w.setpos(start)
                frames = w.readframes(length)

            with wave.open(out_path, "wb") as out:
                out.setnchannels(channels)
                out.setsampwidth(sampwidth)
                out.setframerate(framerate)
                out.writeframes(frames)

            duration = length / float(framerate)
            entry = {
                "id": sid,
                "file": src["file"],
                "container": "wav",
                "sha256": sha256_file(out_path),
                "bytes": os.path.getsize(out_path),
                "sampleRate": framerate,
                "channels": channels,
                "bitDepth": sampwidth * 8,
                "frames": length,
                "durationSec": round(duration, 6),
                "kind": src.get("kind", "loop"),
                "title": src.get("title", ""),
                "origin": src.get("origin", ""),
                "rights": src.get("rights", ""),
                "trim": {"startSec": start_sec, "seconds": None if seconds is None else float(seconds)},
                "source": {
                    "file": os.path.basename(path.replace("\\", "/")),
                    "sha256": sha256_file(path),
                    "bytes": os.path.getsize(path),
                },
            }
            refs.append(entry)
            print("ok  %-26s wav %d ch %d bit %d Hz  %.2fs  -> %s"
                  % (sid, channels, sampwidth * 8, framerate, duration, src["file"]))
        except Exception as exc:  # noqa: BLE001 - report and continue
            failures.append(sid + ": " + str(exc))

    if failures:
        print("FAILURES:")
        for f in failures:
            print("  - " + f)
        return 1

    doc = {
        "schemaVersion": 1,
        "product": spec.get("product", ""),
        "generatedBy": "tools/release/prepare-references.py",
        "description": "Canonical reference inputs (committed). Regenerate only via the sources spec; "
                       "hashes here are the evidence identity - never edit by hand.",
        "references": refs,
    }

    out_json = os.path.join(out_dir, "references.json")
    with open(out_json, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=2, sort_keys=False)
        f.write("\n")

    print("wrote " + out_json + " (" + str(len(refs)) + " references)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
