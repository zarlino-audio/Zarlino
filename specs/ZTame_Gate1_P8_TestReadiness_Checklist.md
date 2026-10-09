ZTAME TEST-READINESS CHECKLIST
==============================
Companion to: specs/ZTame_Gate1_Spec_and_Differentiators.md (Gate-1 spec)
Named P8 test: contract tsk_ztame_p8 - suppression-quality A/B listening test.
Reference for DSP facts: plugins/ZTame/Source/PluginProcessor.{h,cpp} (+ Tests/).

Purpose
-------
One line per DIFFERENTIATOR -> the single satisfiable condition that, on the named P8 test
or an orthogonal measurement, proves the claim. Every row has a deterministic PASS bar so a
reviewer marks PASS/FAIL, never a vibe. Parentheses = declared in Section 3.2 of the spec.

ROW | INNER | DIFFERENTIATOR        | CONDITION TO SATISFY (deterministic)               | TEST | PASS BAR
----|-------|------------------------|-----------------------------------------------------|------|----------
C1  | M     | D1 Ratio-not-level     | 3k -12dB on -12dB pad = no notch; on -30dB pad=notch | MT-4 | both legs hold
C2  | M     | D2 Narrow per-peak     | pad+tone 2k = notch bounded ~2*radius; neighbours >300Hz flat <1 dB | MT-3 | region bounded
C3  | M     | D3 Listen=residual     | Listen-ON out nulls to -80 dB vs (dry - processed)  | MT-7 | null <= -80 dB
C4  | M+H   | D4 True dry/wet/bypass | dry/wet = linear crossfade; bypass+wet=100 exact copy| MT-5 | curve + bit-exact
C5  | M+H   | D5 Selectivity travel  | CCW->CW at fixed sens=50 changes trigger >= 6 dB     | MT-9/MT-N | >= 6 dB AND P8
                                                                                                  |            symptom-(b)
                                                                                                  |            <= 1/12 reported
C6  | M     | D6 Per-bin A/R times   | res appears/vanishes settles ~attack/~release        | MT-6 | at -10dB points
C7  | M     | D7 Freq-adaptive window| 1000&1050Hz = sep hiF context, same loF              | MT-N osc | split matches
C8  | M+H   | D8 Narrow=the benefit  | target clip = lonely notch+unity floor, AND P8 relief | MT-3b+[P8]| notch + >=9/12 relief
C9  | M     | D9 Idle/null do-no-harm| silence/DC/-140dBFS unchanged, >= -120 dB null          | MT-0 | >= -120 dB null
C10 | M+H   | D10 GR-meter truth     | meter 0 on flat pad, rises+decays with resonance     | MT-N obs | follows measured GR
C11 | M     | D11 Bounded cut + knee | ratio sweep gain monotonic, clamped -`range`,no below | MT-2 | clamp exact
C12 | M+H   | D12 "Surgical" claim   | equal relief vs wideband EQ but LESS broadband loss,  | [P8]+MT | relief + lower
                        |           | plus P8 "body kept"                                  |        | total RMS loss

PRE-FLIGHT (blocks rows going green)
------------------------------------
  P1 Add the measurement Tests in ROW C1-C11 to plugins/ZTame/Tests (freeze the DSP laws,
     mirroring ZScorch). None needs a paid render (synthetic/offline). Without them the
     engine-level PASS bars are not auto-locked.
  P2 Only then run P8 contract tsk_ztame_p8 blind against the Section-4 bars; record
     PASS/FAIL/INCONCLUSIVE via the perceptual registry aggregator (>= 12 accepted = validation
     tier; fewer = WEAK evidence, cannot by itself flip the gate).
  P3 Either implement the split-band crossover (currently a STUB in code) or explicitly demote
     that claim from the marketing line. Do not claim split-band behaviour that the code does
     not yet deliver.
  P4 Reconcile the code comment (~3 dB aggressive) vs the running formula (1.5 dB) and pin it
     with the trigger-map test so doc and DSP agree.

OUTCOME
-------
When the 12 rows have their ENGINE bar (P1/MT-x) green AND the four P8-human bars in section 4
clear (no artefact symptom-a, symptom-b floor, >=9/12 relief, body-kept), the readiness
checklist is COMPLETE and ZTame is P8-matched to ship. Zero vibes; every row is a number or a
pre-registered count.

Version 1.0 (companion to Gate-1 spec). Authoring cost: $0.
