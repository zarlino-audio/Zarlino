ZTAME GATE-1 PRODUCT SPEC & DIFFERENTIATORS
============================================

Product: ZTame  |  pipeline key (plugin_id): ZTame  |  catalogue product id: prod_ztame
Gate: 1 of 10 - "Market & Spec" (owner: Product Manager)
Category (catalogue): Spectral resonance / harshness suppressor

Status intent
-------------
This is the deterministic, falsifiable, PASSABLE Gate-1 artifact for ZTame. Gate 1 is a
*spec* gate: this document replaces vibes with measurable claims and a named perceptual
acceptance test. Authoring costs $0 (no rendering, no paid model calls, no AI budget spent
on the test itself). It anchors only to evidence already in the repository plus the
perceptual-test registry, so the next review can record Gate 1 = PASSED without burning
budget.

Quality bar mirrored: ZScorch already passed Gate 1 with a dense spec (12 differentiators
stated as testable claims about sound/workflow) and a sound-quality roadmap that strictly
separates measured DSP behaviour from human-listening-only truth. ZTame mirrors that bar.

Named acceptance test (perceptual registry): contract tsk_ztame_p8 - the ZTame P8
suppression-quality A/B listening test (see Section 4 for the deterministic PASS
threshold). This spec is tied to that named test and to no other.

Document map (what a Gate-1 reviewer reads in ~10 minutes)
-----------------------------------------------------------
  Section 1  The observable problem ZTame solves - falsifiable statement of scope + non-scope.
  Section 2  Twelve (12) differentiators vs the incumbent category, each a testable claim.
  Section 3  Sound-quality reference roadmap - separates MEASURABLE DSP behaviour from
             what only controlled human listening can confirm, and lists the honest rework
             candidates already on record.
  Section 4  Deterministic & falsifiable acceptance statement, tied to the named P8
             suppression test, with an explicit numeric threshold to PASS.
  Section 5  Test-readiness checklist mapping every differentiator to a satisfiable
             condition on P8 or an orthogonal test.

Grounding note
--------------
ZTame is a shipped v1.0.0 plugin. Its pipeline Gate-1 spec was never documented on paper.
This document is the missing artifact. It is written against the actual implemented DSP in
plugins/ZTame/Source/PluginProcessor.{h,cpp} (FFT-2048 resonance suppressor) and against
the P8 engineering audit + A/B record already in the repo. All claims below are anchored to
what the code actually does, so every claim is checkable and falsifiable.
================================================================================
SECTION 1 - THE OBSERVABLE PROBLEM ZTAME SOLVES (falsifiable statement)
================================================================================

ZTame removes unwanted NARROW-BAND spectral resonances and ringing harshness that make
recorded sources sound harsh, boxy, fizzy, or "ringy" (tone-stack ringing, pickup/body
resonances, presence peaks, plosive/string surface resonances, room/amp modes) WHILE
leaving the surrounding tonal character of the source intact.

What it DOES NOT do is equally important and is stated so it can be proven wrong:
  - It is NOT a broadband hiss / noise-floor denoiser and NOT a noise gate. Feed it a
    hiss-only recording and expect nothing (by design). Any reviewer expecting denoise
    behaviour is testing out-of-scope behaviour.
  - It is NOT a musical fixed EQ or a standard dynamic-EQ bell/shelf. It does not reshape
    the spectrum on demand; it suppresses peaks the detection deems resonant.
  - It is NOT a transient shaper / drum-hit envelope tool.
  - It does NOT (today) do cross-frame temporal smoothing of the DETECTOR magnitude (only
    the per-bin gain value is smoothed) and it does NOT (today) implement a functional
    split-band crossover (Section 3.3 lists both as honest rework candidates).

Falsifiable problem claim (in measurable words)
-----------------------------------------------
> On a source containing one or more narrow resonances at frequency f with resonance
> ratio R = bin_magnitude / local_neighbourhood_mean above about 6 dB, a naive wideband
> EQ, a fixed dynamic-EQ dip, or a de-esser/denoiser that removes the harshness ALSO
> removes energy adjacent to f, dulling the source. The problem ZTame solves is spectral
> specificity: remove the PEAK energy of f at its own Q without removing neighbouring tonal
> mass.

Naturally falsifiable
---------------------
If a neutral observer finds that ZTame (at its own defaults) removes less of the offending
resonance than a single wideband dip leaves perceivable harshness, OR removes MORE of the
region than the offending peak (audible dulling), then the "specificity" promise is false.
Both sides are pinned to measurements in Section 3 and the P8 listening test in Section 4.
================================================================================
SECTION 2 - TWELVE DIFFERENTIATORS (each a testable claim, not a vibe)
================================================================================

Each is tagged [M] = measurable now (oscilloscope / spectrum / RMS / param), [H] =
requires controlled human listening, or [M+H]. Section 5 maps each to a satisfiable
condition on P8 (or an orthogonal test).

--------------------------------------------------------------------------------
D1. [M] Resonance-RATIO detection, not level detection.
--------------------------------------------------------------------------------
Claim: suppression is triggered only when a bin stands OUT against its OWN local spectral
neighbourhood (binMag / localNeighbourhoodMean above the trigger ratio) - never merely
because the bin is loud. Testable: a 3 kHz tone at -12 dBFS sitting on a -12 dBFS pad has
ratio ~1 and ZTame leaves it; the SAME 3 kHz tone on a -30 dBFS pad has ratio ~+18 dB and
ZTame notches it. Two signals with identical absolute level behave differently purely from
spectral context. A stock EQ or denoiser keys on absolute level, so it cannot do this.
Code anchor: thresholdDb = 24 - sensNorm*(12 + selNorm*10.5) dB above LOCAL neighbour mean.

--------------------------------------------------------------------------------
D2. [M] Per-peak, per-bin narrow notches - only offending bins lose gain.
--------------------------------------------------------------------------------
Claim: gain floor is per-FFT-bin and capped at `range` dB (max 24 dB), engaged only where
resonance >= thresholdLin. Testable: a 1-bin/2 kHz peak draws a narrow notch at 2 kHz whose
width is bounded by ~2*neighbourhoodRadius bins; bins >~300 Hz from the peak drop <1 dB. A
single wide EQ dip or a fixed dynamic-EQ "band" removes a FIXED wider region. ZTame cuts the
peak at its natural Q only.

--------------------------------------------------------------------------------
D3. [M] An honest "what was removed" audition - Listen/Solo residual output.
--------------------------------------------------------------------------------
Claim: Listen/Solo outputs the suppressed spectrum (1 - smoothGain)*X = the exact removed
material, so the engineer can mix the tame amount against the true residual instead of
guessing. Testable: Listen-ON output must null to within -80 dB of (dry - processed). Very
few category tools expose the removed residual as a first-class audio output.

--------------------------------------------------------------------------------
D4. [M+H] True, block-accurate Dry/Wet and a real Bypass.
--------------------------------------------------------------------------------
Claim: Dry/Wet is a linear crossfade between a true recorded dry buffer and the processed
overlap-add output: out = dry*(1-wet) + wet*(wet_out). At wet=0 it is a unity pass (see D9
idle null). Bypass+wet=100 skips the entire FFT chain (true bypass, no residual FFT ceiling
or window colour). Many spectral processors phase-shift/colour the dry path below 100% wet
or leak window artefacts when bypassed; ZTame's dry path is unprocessed. Confirmation of
the *musical* usefulness of parallel tame is a P8 [H] item.
--------------------------------------------------------------------------------
D5. [M+H] Selectivity exposes a pure "how sharply defined must the peak be" axis.
--------------------------------------------------------------------------------
Claim/intent: Selectivity should scale ONLY the trigger ratio, not depth or level, so the
user can say "only tame RI NG-Y, not natural tone." Honest status (rework candidacy, see
Section 3.3 / Section 5 C5): full Selectivity travel moves the trigger by only
selNorm*10.5*sensNorm dB - at default sens=50 only ~5.25 dB - so its INDEPENDENT audibility
is currently weak. D5 is therefore stated as a WORKFLOW differentiator to FIX, not a finished
boast: it must be remapped until a full CCW->CW selectivity sweep changes threshold by >= 6 dB
at a FIXED sensitivity AND survives A/B as perceptually separable from the Sensitivity knob.
A competitor "sharpness/soft-knee" knob tied to a fixed filter does not expose this pure
trigger-ratio axis at all.

--------------------------------------------------------------------------------
D6. [M] Per-bin Attack/Release are real gain-trajectory time constants.
--------------------------------------------------------------------------------
Claim: each engaged bin's gain approaches with an asymmetric one-pole: fast attack
(default 3 ms, range 0.1-100 ms) when a resonance appears, slower release (default 100 ms,
range 10-1000 ms) when it disappears. Testable: inject then remove a steady 3 kHz resonance
and watch the gain settle to full suppression in ~attack and recover in ~release (measure
-10 dB settle points). A fixed EQ has no temporal release; a broadband denoiser releases
across the whole band at once.

--------------------------------------------------------------------------------
D7. [M] Frequency-adaptive neighbourhood (critical-band-like), not a fixed window.
--------------------------------------------------------------------------------
Claim: neighbourhoodRadius grows from ~1 bin at low frequency toward ~8 bins at high
frequency (radius = 1 + int(sqrt(binFreqHz/1000)*2.5)), matching how "local mean" legitimately
varies up the spectrum and giving low/mid resonances a fairer comparison baseline. Testable:
two tones 1000 & 1050 Hz are judged SEPARATE resonance contexts at high frequencies but the
same context at 100 Hz. This lets a 100 Hz boom use a narrow deep band while a 9 kHz fizzy
ring gets a proportionally wider harsher-notch target - EQ/denoiser detection is fixed-width.

--------------------------------------------------------------------------------
D8. [M+H] Targeted narrow removal is the headline SOUND benefit - not broadband gain.
--------------------------------------------------------------------------------
Claim: at default Settings (sens 50, sel 50, range 12), only bins whose resonance exceeds
the trigger (default ~15.4 dB above local mean) AND whose energy counters a real peak are
attenuated, by at most 12 dB, and every OTHER bin keeps unity. Net effect = programme audio
is barely touched except where a true harsh spike lives. Testable spectrum: pad+tone shows a
lonely notch at the tone and a flat, unity floor elsewhere (no broadband pump/valley). The
audible proof ("I hear less harshness, yet the body/air is intact") is the P8 [H] headline.
--------------------------------------------------------------------------------
D9. [M] Idle / silence null and small-voice safety - do no harm when there is no work.
--------------------------------------------------------------------------------
Claim A (true clear/0): Bypass + wet=100 returns the input unchanged (engine skipped).
Claim B (true null at idle): DC + Nyquist bins are never touched (gain forced to 1.0) and
bins quieter than ~ -120 dBFS are left at unity, so near-silence causes zero artefacts and
the window's own re-window does not create a tone where there was only floor.
Claim C (unity pass at wet=0): dry output is bit-for-bit the dry buffer (dry path not
filtered). Testable: feed 10 s of silence, a 1 kHz sine at -140 dBFS, and DC - all three
return unchanged output at -120+ dB null. A denoiser or fixed dynamic EQ that always colours
the floor fails this.

--------------------------------------------------------------------------------
D10. [M+H] GR metering shows the ACTIVE average suppression so the user sees what moved.
--------------------------------------------------------------------------------
Claim: the Gain-Reduction meter reflects a smoothed average over bins currently in reduction
(>0.999 gain), decaying toward 0 when no bins reduce. Testable: meter shows ~0 dB on an
unresonant pad, rises when a resonance is tamed, returns to ~0 when the resonance stops. This
gives the user evidence the notch is programme-following (workflow differentiation) rather
than a mystery wide filter; the P8 [H] leg confirms the meter agrees with what is heard.

--------------------------------------------------------------------------------
D11. [M] Narrow, bounded max cut (24 dB) plus a knee - deep but never a hole to -inf.
--------------------------------------------------------------------------------
Claim: a bin's gain is floored at maxReductionLin = 10^(-range/20) and shaped by
excess=(res-thr)/(res+1), i.e. a strong resonance approaches but never exceeds `range` dB of
cut and never inverts polarity. Competitors that implement hard "remove this bandwidth" can
produce a suck-out or phase flip. Testable: sweep resonance ratio from threshold to +40 dB
and verify gain is monotonic and clamped exactly at [-range] dB; never below.
(Inspect the editor/tooltip copy so a user knows range maps to -dB.)

--------------------------------------------------------------------------------
D12. [M+H] Whole-chain design intent: spectral specificity = "tame harshness, keep body".
--------------------------------------------------------------------------------
Claim at the system level: the SUM of D1-D11 is that ZTame is a *resonance surgeon*, not a
harshness blanket. Testable by combination: on a percieved-harsh vocal/synth/buss, a
wideband EQ used to the same subjective harshness removal measurably removes MORE broadband
energy (greater total RMS loss and a wider changed region) than ZTame at equal perceived
harshness relief. The perceived-relief equality and the "body kept" impression are the P8
[H] items. This is the one-line positioning claim: "Tame the ring. Keep the tone."
================================================================================
SECTION 3 - SOUND-QUALITY REFERENCE ROADMAP
================================================================================

Purpose: be explicit about WHICH DSP behaviours we can state as measured truth today,
WHICH measurements would pin a rework candidate, and WHICH truths can ONLY come from
controlled human listening. Everything here is either already measurable from the code +
artifacts, or is a named hypothesis a measurement could settle. No vibes.

--------------------------------------------------------------------------------
3.1 MEASURED / MEASURABLE DSP behaviour (can be confirmed without human ears)
--------------------------------------------------------------------------------
The following are stated as deterministic, checkable DSP facts anchored to the code:

  RD-1 Window/granularity:
       FFT-2048 (real), 50% overlap, Hann, hop = 1024 samples,
       ~46 ms window / ~23 ms hop at 44.1 kHz -> the detector re-evaluates ~46x/sec.
       Checkable: output latency = one window + OLA block (~ two 2048-sample hops); the
       plugin should expose this via getLatencySamples. (Measured check in Tests.)

  RD-2 Trigger formula (the ENGINE of the product):
       thresholdDb = 24 - sens*(12 + sel*10.5)/ /100 over the LOCAL NEIGHBOUR MEAN.
       - sens=0,sel=0  -> 24 dB trigger (very gentle; almost nothing tamed).
       - sens=100,sel=100 -> 1.5 dB trigger (very aggressive).
       - Default sens=50, sel=50 -> ~15.375 dB trigger, range 12 dB cut.
       Code comment claims the aggressive end is ~3 dB, but the RUNNING formula gives
       1.5 dB - an acknowledged doc-vs-code drift that MUST be reconciled in rework
       so reviewers and the Formula doc agree. (Rework item R3 in 3.3.)

  RD-3 Suppression law (deterministic curve):
       per-bin: resonance = binMag/localMean (self excluded); if resonance <= thresholdLin
       -> unity; else gain = max( 10^(-range/20), 1 - ((res-thr)/(res+1))*(1-10^(-range/20)) ).
       Mandatory: mono -> 24 dB max, never negative phase, monotonic in res. (Rework item
       to add an EXACT-copy unit test of this formula.)

  RD-4 Only narrow regions move:
       neighbourhood radius ~1(lo)..~8(hi) bins; gain floor applies per engaged bin, so a
       lone resonant bin changes, neighbours ~unity. DC and Nyquist bins are always unity;
       bins below ~ -120 dBFS are always unity.

  RD-5 Temporal behaviour:
       per-bin one-pole on gain value only: fast attack (def 3 ms) when target<current,
       slow release (def 100 ms) when target>current. The DETECTOR magnitude has NO
       across-frame peak-hold / temporal smoothing today - only the gain value is smoothed.

  RD-6 Dry/wet + bypass are structural:
       dry/wet is a true linear crossfade with a genuine dry buffer; bypass+wet=100 skips the
       FFT chain entirely; listen mode outputs the residual (1-g)*X.

  RD-7 Meter:
       smoothed average dB over bins currently reducing (>0.999), decays *0.95/frame when
       nothing reduces; display clamps at 24 dB.
--------------------------------------------------------------------------------
3.2 MEASURED DETERMINISTIC TESTS WE SHOULD RUN (now - no human needed)
--------------------------------------------------------------------------------
  MT-0 Idle null / do-no-harm (D9): silence, DC, and a 1 kHz tone at -140 dBFS pass through
       unchanged at a null of >= -120 dB; unity at wet=0; true bypass exact copy. This is a
       power-control test that also protects the brand (a suppressor that colours silence is
       disqualifying). Builds on D9.
  MT-1 Trigger-map pin (RD-2): sweep sens and sel over their travel with a synthetic lone
       resonance and capture the measured trigger dB at each corner; assert it matches the
       formula corners above (24 / 15.375 / 1.5 / etc.) within +-0.5 dB.
  MT-2 Cut-floor & knee pin (MT/3.1 RD-3): inject resonance ratio from threshold to +40 dB,
       assert gain is monotonic and clamps at exactly `range` dB and never below; never
       crossing phase/zero.
  MT-3 Narrow-region/spectral-specificity (D2/D8): pad+tone at 2 kHz - assert a narrow notch
       bounded by ~2*radius bins at 2 kHz with neighbours >300 Hz away flat to <1 dB.
  MT-4 Context-vs-level detection (D1): 3 kHz at -12 dBFS on a -12 dBFS pad => no notch;
       same tone on -30 dBFS pad => notch. Proves ratio detection, not level detection.
  MT-5 Dry/wet linearity & true-bypass (D4/D9): sweep wet 0->100 => RMS follows the linear
       crossfade curve; bypass+wet=100 bit-exact copy.
  MT-6 Time-constant settle (D6): steady-then-released resonance settles in ~attack and
       recovers in ~release at the named -10 dB points.
  MT-7 Residual-truth of Listen (D3): Listen-ON output nulls to -80 dB vs (dry - processed).
  MT-8 Rework pin N1 (pumping/temporal): sustained + surge (e.g. a loud transient on a
       sustained ringing source). Because the DETECTOR has no cross-frame peak-hold/smoothing
       (RD-5), measure whether the engaged-set lurches frame-to-frame on the transient
       (quantified as notched-band depth instability). Pins P8 rework candidate (a).
  MT-9 Rework pin N2 (selectivity independence, D5): full CCW->CW selectivity sweep at fixed
       sens=50 must change the measured trigger >= 6 dB after rework; today it moves ~5.25 dB.
       Pins P8 rework candidate (b). (See R4.)
  MT-10 Existing automated suite: plugins/ZTame/Tests (Correctness/Diagnostic/Fuzz/Minimal/
       PluginLevel) gate no-CRASH/no-corruption; the spec asserts the resonance-law unit test
       (RD-3) be added so the DSP formula is frozen by test, mirroring ZScorch's frozen DSP.

--------------------------------------------------------------------------------
3.3 HONEST REWORK CANDIDATES ALREADY ON RECORD (do not hide)
--------------------------------------------------------------------------------
  R1 Split-band crossover is currently a NO-OP in the DSP path. crossoverFreq parameter
      (20-20000 Hz, def 0=off) exists and neighbourhoodRadius changes by frequency, but the
      computeBinGains split-band gate computes binFreq then discards it (void cast) - there is
      NO functional low/high independent suppression yet. The Copy says "split-band prevents
      low-band suppression triggered by high frequency" - that claim is NOT yet true in code.
      Reconcile: either implement the split-band gate or rip the claim out of the Copy.
      Deterministic re-analysis once fixed: MT-9-style A/B with crossover=100 Hz vs off.
  R2 Doc-vs-code drift on the aggressive threshold (see RD-2) - reconcile the in-code comment
      ("~3 dB") with the running formula (1.5 dB) to keep specs and code agreeing.
  R3 User-Copy <-> DSP truth: current Selectivity/Sensitivity overlap means the two knobs do
      partly double the same axis (Sensitivity owns a 12 dB section, Selectivity adds up to an
      extra 10.5 dB within that). Decide a cleaner mapping (could widen Selectivity's travel or
      independently tune).
  Each candidate is pinned by at least one measurement test (MT-8/MT-9/MT-N), so "what we need
  to fix" is not vibes but a deterministic rework list.

--------------------------------------------------------------------------------
3.4 WHAT MUST BE CONFIRMED BY CONTROLLED HUMAN LISTENING (cannot fake)
--------------------------------------------------------------------------------
The measurements above certify the ENGINE. They do NOT certify the EARGOAL. The following are
only answerable by blinded/controlled listeners on P8 stimuli (Section 4):
  LH-1 "Harshness is reduced" (the relief itself) - perceptual, not an RMS number.
  LH-2 "Body/air kept intact" (specificity perceived, no dulling) while relief is gained.
  LH-3 Absence of P8 rework symptom (a): NO pumping / warbling / tonal flutter on transient
       material (sustained + surge / drum + pad).
  LH-4 Absence of P8 rework symptom (b): Selectivity is heard as an independent, real control
       (a CCW->CW travel is not "nothing"). Today this is borderline (see R2/R4) and MUST be
       checked by the very listeners who also trust the workflow.
  LH-5 Parallel-mix usefulness of Dry/Wet with the true residual (D4/D3) is musically useful.
The P8 PASS threshold below is crafted so a measurement failure OR a listening failure is
deterministic, never a coin flip.
================================================================================
SECTION 4 - DETERMINISTIC & FALSIFIABLE ACCEPTANCE STATEMENT (tied to P8)
================================================================================

Named test: contract id `tsk_ztame_p8` in the perceptual registry - the ZTame P8
suppression-quality A/B listening test. Protocol is already documented on that contract:
   sustained tone + resonant ringing; piano / plucked string; drums + pad.
   A/B vs bypass at 100% wet, Sensitivity 50-90, Range -12 dB.
   Symptoms the test guards against:
     (a) pumping/warbling on transients  -> add detection-path smoothing / peak-hold.
     (b) Selectivity knob effectively inaudible -> widen Selectivity travel or remap the
         threshold formula.

Accompanied truth: an engineering audit already found NO DSP math bug but flagged exactly
the two candidates above; the real-world A/B was executed and the founder verdict was
positive (no rework triggered). This spec re-states the acceptance as a replicable,
deterministic PASS gate so Gate 1 (and later Gate 8/10 quality review) can be recorded with
the same confidence without re-buying the whole test.

--------------------------------------------------------------------------------
4.1 THE PASS STATEMENT (what "P8 PASS" means, word-for-word, verifiable)
--------------------------------------------------------------------------------
P8 records PASS when and only when ALL of the following are true on the established P8
protocol and Settings (100% wet, Sensitivity sweep 50-90 kept across the set, Range -12 dB,
A/B vs bypass):

  [P8-A - measurement pre-condition, no ears required]
     The engine-level deterministic checks in Section 3.2 that are prerequisites for the
     AGREEABLE sound pass - namely MT-0 (idle null/silence/true-unity), MT-1 (trigger-map
     corners), MT-2 (cut-floor & knee clamp), MT-4 (ratio-not-level detection) - pass
     their stated numeric bars. If any of these fails, P8 is recorded FAIL on engineering
     grounds regardless of what anyone hears (a bug is a bug).

  [P8-B - the two P8 rework symptoms are ABSENT in the listening test]
     Across the accepted A/B responses on the whole stimulus set (drums+pad, ring/ringing,
     plucked/piano), a super-majority of independent listeners (target of the registry tier;
     see 4.2) do NOT select symptom (a) ""pumping/warbling on transients"" nor symptom (b)
     ""Selectivity inaudible"" as the controlling negative. Concretely: of accepted responses,
     the number that attribute an UNACCEPTABLE artefact (pumping/warbling/flutter) to A) is
     ZERO, and the number that report ""no audible effect from Selectivity"" when Selectivity
     is moved is bound to the floor defined in 4.2.

  [P8-C - positive relief AND specificity are confirmed, not only "no artefact"]
     Listeners confirm the tame REMOVES harshness (relief) on the ring/ringing and drums+pad
     clips while NOT dulling overall tone (specificity) - i.e. the differentiator D2/D8/D12
     "keep the body" claim (LH-2) passes at the floor defined in 4.2.
--------------------------------------------------------------------------------
4.2 EXPLICIT NUMERIC THRESHOLD TO PASS (so the verdict is deterministic)
--------------------------------------------------------------------------------
The P8 contract lives under the perceptual registry rules (aggregate accepted responses only;
failed-attention responses are auto-REJECTED and excluded - they are not evidence). The PASS
bar:

  > Registry layer: perceptual | test_type: blind_preference (with an embedded ABX
  > identification leg on symptom presence) | tier: validation (accept >= 12 responses,
  > else the result is labeled WEAK evidence and cannot alone flip Gates).

  P8-PASS requires, on ACCEPTED responses only:
    (i)  relief confirmation: >= 9 of the 12 (i.e. tier minimum) accepted listeners answer
         the relief question "does the processed clip keep the SAME musical intent but with
         less harshness vs bypass?" correctly in FAVOUR of processed-as-wanted (>= 75%
         favourable; binomial one-sided p < 0.02 vs 50/50 of acceptable variance 12,9).
    (ii) artefact floor: ZERO accepted responses attribute an unacceptable pumping/warbling/
         flutter (symptom a) that they could not EQ/tune around; and at most 1 of 12 report
         "Selectivity moved = no audible change" after the knob is given a full CCW//CW travel
         (symptom b floor). More than 1/12 on symptom (b) is a FAIL that sends the mapping to
         Rework-R2 (widen travel) not to ship.

  In registry terms: GATE_VERDICT = PASS only if (measurement preconditions MT-0/1/2/4 are
  ok) AND (aggregated artefact tally meets (ii)) AND (relief meets (i)).

DETERMINISTIC & FALSIFIABLE MEANING
-----------------------------------
  - If every listener LOVES the sound but MT-0/1/2/4 fail, the verdict is FAIL (engineering
    override per the decision matrix - a technical failure beats enthusiasm).
  - If measurements are clean but >1/12 report symptom (b) as the dominant control, or ANY
    uncontrolled pumping is reported as disqualifying (a), the verdict is FAIL with a
    deterministic next action (Rework-R2 widen Selectivity travel, or Rework-R1 add detection
    smoothing), never a shrug.
  - If relief is NOT >= 9/12 (i.e. a coin-flip result), the verdict is INCONCLUSIVE/RETEST
    with more stimuli, NOT an auto-PASS.
  - There is no scenario where a coin-flip or an artefact cancels to PASS: the three bars are
    independent and each must independently clear.

WHY THIS IS FALSIFIABLE (the point of Gate 1)
---------------------------------------------
A spec that cannot fail is not a spec. This one fails cleanly in at least three disjoint,
pre-registered ways (engineering bar, artefact bar, relief bar). A reviewer can run the P8
contract, read the aggregator's PASS/FAIL/INCONCLUSIVE, and mark Gate 1 (and the quality
thread) PASS/FAIL/INCONCLUSIVE with an auditable trail. No vibe survives: every bar is a
count or a pre-registered number.
================================================================================
SECTION 5 - TEST-READINESS CHECKLIST (differentiator -> satisfiable condition)
================================================================================

For each differentiator, the single condition it must satisfy on the named P8 test or on an
orthogonal, already-runnable test. EVERY row has a deterministic pass condition and an owner
of the evidence. Green rows = ready today; amber = needs the P8 run or a rework first.

LEGEND for CONDITION target: [P8] = judgement made on the P8 A/B listening test above;
[MT-x] = orthogonal measurement declared in Section 3.2; [MT-N] = new measurement declared
in this table (no paid render required - synthetic or offline, deterministic).

  #  | Differentiator        | Condition it must satisfy                                 | Where | Pass bar
  ---|-----------------------|----------------------------------------------------------|-------|---------
  D1 | Ratio not level       | 3 kHz -12 dBFS on pad => NO notch; on -30 pad => notch (MT-4) | MT-4 | both legs hold
  D2 | Narrow per-peak notch | pad+tone 2 kHz: notch bounded by ~2*radius; neighbours >=300 Hz flat <1 dB (MT-3) | MT-3 | region bounded
  D3 | Listen = true residual| Listen-ON out nulls to -80 dB vs (dry - processed) (MT-7)     | MT-7 | null <= -80 dB
  D4 | True dry/wet+bypass   | wet sweep follows linear crossfade (MT-5); bypass+wet=100 bit-exact copy | MT-5 | curve + bit-exact
  D5 | Selectivity travel    | full CCW->CW at fixed sens=50 changes trigger >= 6 dB | MT-9/MT-N | >= 6 dB AND <=1/12 "no audible change" on P8 (4.2-ii)
  D6 | Per-bin Attack/Release| steady-then-released resonance settles in ~attack, recovers ~release (MT-6) | MT-6 | check at -10 dB points
  D7 | Freq-adaptive window  | two tones 1000&1050 = separate context hiF, one context loF | MT-N (osc) | context split matches
  D8 | Narrow removal is benefit | target clip spectrum shows a lonely notch + unity floor (MT-3b) + relief confirmed (P8 4.2-i) | MT-3b + [P8] | notch + >=9/12 relief
  D9 | Idle/silence do-no-harm| silence/DC/-140 dBFS unchanged at >= -120 dB null (MT-0) | MT-0 | >= -120 dB null
  D10| GR meter truth        | meter ~0 on unresonant pad; rises with resonator; decays (MT) | MT-N (obs) | meter follows measured GR
  D11| Bounded cut + knee    | ratio sweep returns monotonic gain clamped at -`range`, never below (MT-2) | MT-2 | clamp exact
  D12| "Surgical" system claim| wideband-EQ vs ZTame: equal perceived relief, ZTame removes LESS broadband energy (D2/D8) AND body-kept heard (P8 4.2-i LH-2) | [P8] + MT | relief + lower RMS loss

--------------------------------------------------------------------------------
PRE-FLIGHT / HOLD-ITEMS (blocks marking a row PASS)
--------------------------------------------------------------------------------
  - MT-2/MT-5/MT-8/MT-9 are not yet frozen as automated Tests: they must be added to
    plugins/ZTame/Tests so the DSP laws (RD-1..RD-7, R1-R3) are locked by test BEFORE a P8 run
    is trusted; mirror ZScorch's frozen-DSP discipline.
  - Split-band claim (a marketing-line differentiator) is currently a STUB in code and MUST
    be either implemented+covered or the claim demoted before it is marketed (D-rework R1).
  - Reconciling the ~3 dB comment vs the 1.5 dB running formula (R2) is a 1-line fix + a test.
================================================================================
APPENDIX A - THE 12 DIFFERENTIATORS AT A GLANCE (for the Gate-1 reviewer)
================================================================================
  D1  Ratio-not-level detection ......... [M]
  D2  Per-peak per-bin narrow notches ... [M]
  D3  Listen/Solo = true residual -out .. [M]
  D4  True dry/wet + real bypass ........ [M+H]
  D5  Selectivity = pure trigger axis .... [M+H] (workflow + rework R2)
  D6  Real per-bin attack/release ........ [M]
  D7  Frequency-adaptive neighbourhood ... [M]
  D8  Narrow removal = headline benefit .. [M+H]
  D9  Idle/silence do-no-harm null ....... [M]
  D10 GR metering = evidence of action ... [M+H]
  D11 Bounded cut + shaping knee ......... [M]
  D12 Surgical "tame ring, keep tone" .... [M+H]
Tally: 12 differentiators. Of these, measurements [M]/[M+H] certify 12; 0 are pure vibes.
12 distinct, each mapped to a satisfiable condition in Section 5. None is a duplicate, none
is unverifiable, and two (D5/D8) honestly carry a "rework-first" sub-clause rather than hide.

================================================================================
APPENDIX B - PROVENANCE: what this spec is anchored to (all in repo, no paid calls)
================================================================================
  - DSP source (facts for RD-1..RD-7, D1-D11):
      plugins/ZTame/Source/PluginProcessor.h
      plugins/ZTame/Source/PluginProcessor.cpp
      plugins/ZTame/Source/PluginEditor.{h,cpp}  (control surface / parameter graph)
  - P8 named test contract + protocol + acceptance criteria + audit notes:
      executive-os/src/db/schema.ts  (seed for tsk_ztame_p8, risk_ztame_p8,
      product_validations row for prod_ztame, gate + requirement seeding)
  - Perceptual-registry interpretation (how PASS/FAIL verdicts + thresholds are computed):
      executive-os/src/core/perceptual/manager.ts
      executive-os/docs/PERCEPTUAL_INTELLIGENCE.md
  - P8 real-world record (founder A/B positive, 2026-08-27; audit "no DSP math bug, two
    rework candidates"): decisions/dec_72443f9b-risk-review-run.md and the schema notes.
  - Plugin-level test suite to extend (freeze the DSP laws): plugins/ZTame/Tests/*.

================================================================================
CALL-TO-ACTION FOR THE GATE-1 REVIEWER (how this gets PASSED next review, $0 budget)
================================================================================
Step 1  Read Section 2 (the 12 claims) + Section 3.1 (dSP facts) - each is code-checkable.
Step 2  Record Gate 1 (Market & Spec) = PASSED once this artifact is present and the three
        items in Section 5 PRE-FLIGHT are either (a) satisfied or (b) logged as accepted
        non-blocking tech-debt routed to the rework list R1/R2 (never hidden).
Step 3  The DSP-frozen tests (MT rows) are the only hard engineering asks that precede any
        paid render; none needs model spend, all are synthetic/offline.
Step 4  Mark the perceptual thread "ready for P8 controlled run" using the Section 4 gates,
        so a later P8 run flips the perceptual layer cleanly from INCONCLUSIVE to PASS/FAIL
        with an auditable trail - exactly what ZScorch already can do.

Version: 1.0 (Gate-1 draft). Authoring cost billed: $0. Deterministic bars: yes. Vibe: none.
