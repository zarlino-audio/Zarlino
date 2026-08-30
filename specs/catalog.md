# Zarlino Plugin Catalog — Authoritative Per-Plugin Spec

> Last updated: 2026-08-30
> One authoritative spec per product. Each entry defines the *minimal* feature
> set (the bar a plugin must clear to ship) and its *differentiator* features
> (the opinionated character / high-value workflow that sets it apart).
>
> Strategy anchor for every product: **professional finishing tools at an
> accessible price for emerging producers.**
>
> Versioning: `status` is one of CONCEPT → PLANNED → IN-THE-LAB → SHIPPED.
> A product must have its acceptance gates green before `status` is SHIPPED.

--------------------------------------------------------------------------

## ZTame — "Dynamic Tone Tamer"  (Category: dynamics)

1. **Name:** ZTame
2. **Category:** dynamics
3. **Positioning:** A transparent-yet-fast dynamics / tone-taming processor that
   lets emerging producers get a polished, controlled mix bus without fighting a
   full mastering chain — professional finishing at an accessible price.
4. **Minimal feature set (must-haves):**
   - Stereo dynamics core: compressor + peak limiter (threshold, ratio, attack,
     release, makeup gain, knee).
   - Transparent release shaping that keeps low end solid during pumping
     programme material.
   - Automatic makeup gain so "louder and tamer" is one gesture.
   - True-peak-aware output ceiling for loudness-law-abiding masters.
   - Dry/wet mix and master bypass (delay-compensated).
   - Full APVTS state save/restore; automatable params.
   - Input / output / gain-reduction meters.
5. **Differentiator features (why pay vs stock/free):**
   - A tone-shaping sidechain (high-pass / low-pass detector de-emphasis) that
     tames loud parts without dulling the top end — glue-and-clear workflow
     stock compressors lack.
   - Program-dependent release presets tuned to real mix material, giving
     musical behaviour on the first knob move.
   - Curated finishing presets for drum bus, vocal bus, and master glue.
6. **Target user:** Emerging producers finishing their first releases who want
   quick, convincing mix/master glue without a mastering engineer.
7. **Intended price tier:** Entry tier (~$29–$49).

--------------------------------------------------------------------------

## ZScorch — "Multi-Band Harmonic Saturator"  (Category: saturation)

1. **Name:** ZScorch
2. **Category:** saturation
3. **Positioning:** A multi-band harmonic saturator that makes mixes "sound
   expensive" with pleasing glue, air, and stereo movement — a one-stop
   character finisher at an accessible price for emerging producers.
4. **Minimal feature set (must-haves):**
   - Signal chain: input → pre-EQ → 4-band LR4 crossover → per-band saturation →
     recombine → post-EQ → output gain → limiter → dry/wet.
   - 4-band Linkwitz-Riley crossover (24 dB/oct), phase-coherent, mono-sum safe.
   - Per-band saturator engine (Tube/Tape/Transistor/Germanium/Diode/Wavefold)
     with Drive, Asymmetry, Mix, Solo, Mute.
   - Pre & post emphasis EQ (RBJ shelf/bell with enable toggles).
   - Output stage: gain, soft-clip ceiling, dry/wet, master bypass.
   - Full APVTS state save/restore; automatable params.
   - DSP correctness suite + hearing/mono-sum QA evidence.
5. **Differentiator features (why pay vs stock/free):**
   - Opinionated "character remaster" tonal curves tuned by default for gluey
     low end + airy highs — an audible, musical starting point.
   - A dedicated width/size stage that widens/narrows stereo with phase-safe
     mono-sum widening — "moves air" out of one knob.
   - Curated named presets tuned to real material (vocal, drum bus, master
     glue, pads).
   - Per-band harmonic *order* shaping (2nd vs odd bias) for depth from subtle
     warm glue to aggressive excitement without more bands.
6. **Target user:** Emerging producers who want fast, audible polish on tracks,
   busses, or the master without deep analog-chain expertise.
7. **Intended price tier:** Flagship tier (~$49–$79).

--------------------------------------------------------------------------

## ZVocals — "Vocal Presence & Polish"  (Category: vocal)

1. **Name:** ZVocals
2. **Category:** vocal
3. **Positioning:** A dedicated vocal chain that takes a raw recorded vocal and
   gets it sitting up front, clear, and radio-ready — pro vocal finishing at an
   accessible price for emerging producers.
4. **Minimal feature set (must-haves):**
   - De-esser (sidechain-band compressor on sibilance) with frequency + depth.
   - Breath-gain stage to smooth intakes between phrases.
   - Gentle presence EQ (high-shelf air + vocal-mud cut in low-mids).
   - Plosive/pitch-hygiene stage (pop/plosive reduction).
   - Headroom / output stage with dry/wet and master bypass.
   - Full APVTS state save/restore; automatable params.
5. **Differentiator features (why pay vs stock/free):**
   - A one-knob "consistency" depth control that balances de-essing, presence
     lift, and dynamics musically — the "sounds pro" preset experience stock
     chains can't match.
   - Vocal-tuned detector curves that preserve sibilant "s" clarity while
     de-essing (no dulling).
   - Curated starting points: close pop, bright female, warm male,
     podcast/voice-over.
6. **Target user:** Emerging producers and home vocalists who record alone and
   want a competitive modern vocal sound fast without stacking stock plugins.
7. **Intended price tier:** Entry tier (~$29–$49).

--------------------------------------------------------------------------

## PresenceField — "Stereo Field & Presence Expander"  (Category: utility)

1. **Name:** PresenceField
2. **Category:** utility (stereo / width)
3. **Positioning:** A stereo field and presence tool that widens, centres, and
   lifts the sense of space in a mix while staying mono-compatible —
   accessible pro finishing for emerging producers who want a bigger, clearer
   image.
4. **Minimal feature set (must-haves):**
   - Mid/side width control (mid level, side level, wide stereo spread).
   - Stereo balance / pan-law tools (swap, phase flip, L-R swap).
   - Presence filter that lifts mid-range without harshness.
   - Mono-compatibility / correlation meter with mono-check button.
   - Output stage: gain, dry/wet, master bypass.
   - Full APVTS state save/restore; automatable params.
5. **Differentiator features (why pay vs stock/free):**
   - Phase-safe, frequency-aware widening that prevents mono-collapse when
     collapsed — a guarded width engine, not naive haas/reverb widening.
   - A "field tamer" that re-centres overly wide mixes (common in emerging
     producers' roughs) while preserving perceived depth.
   - Real-time correlation visuals that teach the mix's mono safety.
6. **Target user:** Emerging producers who finalise in headphones and need to
   trust their width survives club/system mono playback.
7. **Intended price tier:** Entry tier (~$29–$49).

--------------------------------------------------------------------------

## MyFirstEQ — "Intro Parameter EQ"  (Category: EQ)

1. **Name:** MyFirstEQ
2. **Category:** EQ
3. **Positioning:** A friendly, immediately readable parameter EQ that gets
   emerging producers shaping tone for the first time with pro-shaped filters —
   accessibility-first finishing, not a wall of bells and notches.
4. **Minimal feature set (must-haves):**
   - Multiple fully parametric bands: bell, low shelf, high shelf, LP/HP, each
     with frequency, gain, Q.
   - Visible frequency-response display (analysis/zoomable).
   - Per-band bypass + global EQ bypass.
   - Presets for common track types (kick, snare, vocal, bass, bus).
   - Output gain trim to compensate boosts/cuts.
   - Full APVTS state save/restore; automatable params.
5. **Differentiator features (why pay vs stock/free):**
   - "Nice by default" high-shelf/air and low-mud-cut curves that sound musical
     without careful sweeping.
   - Teaching-first, colour-blind-friendly UI with clear filter-type icons.
   - A one-click "tone lift" scene preset that demos a good finish immediately.
6. **Target user:** Emerging producers / first-time mixers who want pro-shaped
   EQ actions with visible, repeatable results.
7. **Intended price tier:** Entry tier (~$19–$29).

--------------------------------------------------------------------------

## RACK_AI — "AI-Assisted Finishing Rack"  (Category: analysis)

1. **Name:** RACK_AI
2. **Category:** analysis (AI-assisted finishing)
3. **Positioning:** An AI-assisted finishing rack that listens to a track or
   bus and suggests a professional starting chain (EQ, compression, saturation)
   — turning analysis into action for emerging producers at an accessible price.
4. **Minimal feature set (must-haves):**
   - Audio analysis engine measuring loudness (LUFS), spectral balance,
     dynamic range, stereo width, and key tonal issues (mud, harshness,
     thinness).
   - A suggestion engine proposing an ordered chain of processing moves.
   - One-click application of suggestions to host parameters.
   - Real-time meters (LUFS / spectrum / goniometer).
   - Undo/revert for applied suggestions.
   - Full APVTS state save/restore.
5. **Differentiator features (why pay vs stock/free):**
   - Explainable, musical suggestions (not black-box "magic"): each recommended
     move is shown with the reason grounded in the measurement.
   - One-click chain construction that lands a tasteful, finished starting
     point users can then tweak.
   - Instrument- and material-aware presets (vocal, drum bus, master, synth).
6. **Target user:** Emerging producers who hear "something's off" but don't yet
   know which knob to grab — analysis that teaches while it fixes.
7. **Intended price tier:** Mid tier (~$49–$69).
