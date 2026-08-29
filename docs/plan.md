# Vellum — a colorless formant shifter

## The brief

A formant shifter for monophonic vocals that does not sound robotic. Formant
only; pitch is never touched. Same house as FrostyEQ: JUCE, C++20, VST3 + AU +
Standalone, a JUCE-free DSP core that the tests and the measurement harness can
drive directly.

"Colorless" is the whole point, so it is worth stating as a hard requirement
rather than an aspiration:

> At shift = 0 the plugin is bit-transparent apart from a reported latency.
> Not "very close" — the output samples equal the input samples.

That is what it measures: the null against the delayed input is **exactly zero
error**, every sample, not merely below some threshold. The whole path from the
analysis window to the overlap-add accumulator runs in double, so the only
single-precision rounding left is the final store to the host's buffer — and a
double-precision result that differs from the input by 1e-16 rounds to the very
same float. Transparency stops being a tolerance and becomes an identity.

Everything in the architecture below falls out of taking that seriously.

---

## Part 1 — What the artists are actually doing

### The tool

Nearly all of the rap and hyperpop formant sound traces back to one plugin:
**Soundtoys Little AlterBoy**, a monophonic voice manipulator with independent
Pitch and Formant controls plus a Drive stage. It is the plugin named in
essentially every teardown of the Playboi Carti / Opium vocal chain, where the
recipe is pitch down 3–5 semitones for the "demonic" voice, or formant down
with pitch held for the bigger-but-still-him effect, then hard Auto-Tune on top.
Cochise sits in the same lineage. Waves SoundShifter, Auto-Tune's Throat /
formant control, Melodyne's formant handles and MAutoPitch's formant shift are
the other common routes.

### The two distinct sounds

It matters that these are different effects, because they fail differently:

1. **Pitch + formant moving together** (plain pitch shift) — the chipmunk /
   demon axis. Everyone hears it as "pitched". Carti's lower register is often
   this.
2. **Formant alone, pitch fixed** — the melody is untouched but the singer
   appears to have a longer or shorter vocal tract. Shift down and the same
   performance reads as a physically larger person; shift up and it reads
   smaller and younger, without turning into a chipmunk. This is what makes
   brakence's vocals on *hypochondriac* "expand, shrink, sprout and wither"
   while the tune stays put, and it is the only mode this plugin implements.

The physical intuition for (2): a vowel's identity comes from the resonances of
the vocal tract, and those resonances scale roughly inversely with tract length.
A 17 cm tract and a 15 cm tract produce the same vowel with all formants about
13 % apart. The vocal folds, meanwhile, do whatever they want independently.
Formant shifting is a synthetic change of tract length. Nothing else about the
performance should change.

### Why the effect is worth having as a "clean" plugin

The artists above mostly want the artifacts. Carti's sound is not hurt by a
plugin that sounds synthetic; that is the aesthetic. But the same control used
gently — a couple of semitones to sit a double under a lead, or to make a
comped line sound like one person, or to widen a single tracked voice into a
family of voices — needs to be inaudible as a process. Every shifter tested for
this project audibly degrades the signal at *zero* shift, which is the tell that
the process is destructive rather than corrective.

---

## Part 2 — Why formant shifters sound robotic

Six separate causes, all of which are avoidable. This list is the design spec in
negative form.

### 1. Resynthesis

The common implementation is a phase vocoder: analyse, modify, and rebuild the
signal from magnitudes and estimated phases. Rebuilding is where the damage
happens. Phase relationships between harmonics are what make a glottal pulse a
pulse; a vocoder that reconstructs phase per-bin loses the vertical coherence
across bins and the pulse smears into a buzz. This is the classic "phasiness",
and it is present at *any* shift setting, including none, because the signal was
taken apart and put back together either way.

**Vellum never resynthesizes.** See Part 3.

### 2. Envelope estimators that track harmonics

To move an envelope you must first have one. Cheap estimators — a low-order LPC
fit, or a fixed-width smoothing of the magnitude spectrum — do not separate the
vocal tract's response from the harmonic comb of the source. At high pitch the
harmonics are far apart and the "envelope" ends up following individual
partials. Shifting then drags partials around relative to each other, and you
hear beating, chorusing, and pitch instability that was never in the input.

**Vellum uses the True Envelope estimator** (Röbel & Rodet), an iterative
cepstral method whose order is locked to the measured f0 so it is
mathematically incapable of resolving the harmonic spacing.

### 3. Scaling the whole spectrum uniformly

A voice's spectrum is not all vocal tract. It is a source (the glottal pulse,
with its own roughly −12 dB/octave tilt), times the tract response, times the
radiation characteristic. Only the middle term should move. Shifters that
multiply every frequency by the ratio drag the source tilt and the radiation
term along with the formants, which is why shifting down sounds boomy and dull
and shifting up sounds thin and brittle — the tonal balance changed on top of
the formants moving.

**Vellum warps only a band**, with a low pivot below which nothing moves and a
high pivot above which the shift tapers back out.

### 4. Dragging the region around the fundamental

The spectral envelope is steep below the first formant. Shifting it there
changes the gain applied to the fundamental itself by many dB, so the voice
changes weight and level as you turn the knob, and low notes and high notes get
different treatment. The pivot in (3) fixes this too: the fundamental keeps the
gain it arrived with.

### 5. Treating consonants like vowels

`s`, `sh`, `f`, `t` are not vocal tract resonances in the same sense — they are
noise sources at a constriction near the front of the mouth, and the cavity
behind the constriction barely contributes. Scaling them by the same ratio as
the vowels is what produces the lisp and the robotic chirp on sibilants that
gives the effect away instantly.

**Vellum measures voicedness per frame** (free, from the same cepstrum used for
f0) and pulls the shift back toward zero on unvoiced frames, by a settable
amount.

### 6. No loudness compensation

Moving an envelope over a fixed source changes how much energy lands where the
source has energy, so the level moves with the knob. You then can't A/B the
setting honestly, and louder wins.

**Vellum computes the broadband gain the correction filter would apply to
*this* signal and takes it back out.**

---

## Part 3 — The architecture

### Formant shifting as a correction filter

Standard approach: separate source from filter, warp the filter, resynthesize
source × warped filter. The resynthesis is the problem.

Vellum's approach: separate source from filter, warp the filter, then compute
the **difference between the warped filter and the original filter** and apply
that difference to the untouched input signal.

Writing `E(f)` for the estimated spectral envelope and `α` for the shift ratio,
the correction filter is

```
H(f) = E(f/α) / E(f)              (a ratio of magnitudes; in dB, a subtraction)
```

and the output is simply `X(f)·H(f)`. The source is never estimated, never
extracted, and never rebuilt. Phase is untouched — `H` is real and non-negative,
applied zero-phase. Transients, breath, noise floor, room, and the harmonic
phase coherence that makes a voice sound like a voice all pass through
unmodified. The only thing that happens to the signal is a gentle,
slowly-varying EQ curve.

Three consequences worth naming:

**Colorlessness is structural, not tuned.** At α = 1, `E(f/α) = E(f)` and so
`H(f) ≡ 1` exactly, for every bin, in every frame, regardless of what the
envelope estimator did. Estimation errors cancel in the ratio. The plugin at
zero shift is a delay. There is a test that asserts this to −300 dB.

**Estimator errors are second-order everywhere else too.** If the envelope is
imperfect, `H` is the ratio of two similar imperfect curves, so the error
largely cancels rather than accumulating. A resynthesis architecture has no such
cancellation — whatever the estimator got wrong is what you hear.

**Window length no longer smears the signal.** In a phase vocoder, a long window
smears real transients, so you trade frequency resolution against consonant
clarity. Here the window only sets how fast the *filter* tracks. A 43 ms window
means the EQ curve is averaged over 43 ms and updated every 11 ms; the consonant
underneath it is not touched at all. That lets Vellum use a window long enough
to resolve a male fundamental without the usual cost.

### Signal flow

```
in ──┬─────────────── delay (L−hop) ──────────────────────┐
     │                                                    ├── mix ── trim ── out
     └─ STFT ─ envelope ─ f0/voicing ─ warp ─ H(f) ─ ×X ──┘
              analysis                          WOLA
```

The dry path is delayed to match, so Mix is a true crossfade between the input
and a filtered copy of the same samples — no comb filtering at intermediate mix
values, which is another common tell.

### Frames

| Tracking | Window `L` | FFT `N` | Hop | Latency @48k |
|---|---|---|---|---|
| Fast   | 1024 | 2048 | 256 | 1023 smp — 21.3 ms |
| Normal | 2048 | 4096 | 512 | 2047 smp — 42.6 ms |
| Smooth | 4096 | 8192 | 1024 | 4095 smp — 85.3 ms |

Latency is `L − 1`, not the `L − hop` that gets quoted for overlap-add: a slot
in the accumulator is complete only after the last frame covering it lands, and
output leaves at a constant rate, so the delay is set by the *oldest* sample of
each emitted batch. It is reported to the host for delay compensation and
asserted against a measured impulse in the tests.

`N = 2L` is not decoration. Multiplying a spectrum by `H` is a circular
convolution in time; it is only equal to the honest linear convolution if the
windowed frame plus the filter's impulse response fits inside `N`. The frame is
`L` long and `H`'s impulse response is bounded by twice the cepstral order, so
zero-padding to `2L` and keeping the order under `L/2` makes the wrap-around
term identically zero. Most spectral processors skip this and eat the time
aliasing as a low-level buzz. Hann analysis and Hann synthesis at 75 % overlap,
with the overlap sum measured at runtime and divided out.

### Envelope estimation, f0, and voicing

One real cepstrum per frame gives all three.

1. `V(k) = log|X(k)|`.
2. Cepstrum `c = IDFT(V)`. The harmonic comb in `V` has period f0, which appears
   in `c` as a peak at quefrency index `fs/f0` — the pitch period in samples.
3. Search that peak over the allowed f0 range. Its height above the local floor
   is the **voicedness**; its position is **f0**. A median over recent frames
   rejects octave errors.
4. Lifter order `p = κ·fs/f0`, `κ ≈ 0.55` by default (the *Resolution* control).
   Below the harmonic quefrency by construction, so the envelope cannot follow
   partials.
5. **True Envelope**: iterate `C ← lifter_p(max(V, C))` from `C = lifter_p(V)`.
   Each pass fills the valleys between harmonics without pulling the curve down
   into them, converging on the upper envelope. Eight passes or a 0.5 dB
   convergence threshold.

Unvoiced frames have no meaningful f0, so `p` falls back to a fixed moderate
order and the shift is scaled by `1 − consonantPreservation·(1 − voicedness)`.

### The warp

A uniform `f/α` would violate Part 2 items 3 and 4, so the ratio is ramped in
and out in log-frequency:

```
s(f) = ln(α) · w(f)          w: 0 below the low pivot, 1 across the formant
g(f) = f · exp(−s(f))           band, tapering to 0 above the high pivot
E_target(k) = E(g(f_k))         (linear interpolation in frequency)
```

`w` uses raised-cosine ramps, so `g` is smooth and, over the supported range, is
checked for monotonicity when the table is built. Defaults: low pivot 120 Hz
(fundamental protected, F1 upward free to move), high pivot 6 kHz (sibilance and
air keep their own balance).

### Level match

`g = sqrt( Σ|X|² / Σ|X·H|² )`, smoothed, applied to the wet path. Uses the
signal's own spectrum rather than a flat assumption, so it compensates the
loudness change this voice actually experiences. On by default, switchable.

---

## Part 4 — Controls

| Control | Range | Default | Notes |
|---|---|---|---|
| Shift | −12 … +12 st | 0 | The formant ratio, `α = 2^(st/12)`. Pitch is never touched. |
| Low Pivot | 20 … 500 Hz | 120 Hz | Below this the envelope does not move. Protects the fundamental. |
| High Pivot | 2 … 20 kHz | 6 kHz | Above this the shift tapers out. Protects sibilance and air. 20 kHz ≈ off. |
| Consonants | 0 … 100 % | 75 % | How far to back the shift off on unvoiced frames. |
| Resolution | 0.3 … 0.9 | 0.55 | Cepstral order as a fraction of the pitch period. Higher tracks formants more sharply and risks touching partials. |
| Tracking | Fast/Normal/Smooth | Normal | Window length; trades latency against envelope stability. |
| Level Match | on/off | on | Take the filter's broadband gain back out. |
| Mix | 0 … 100 % | 100 % | Delay-matched crossfade. |
| Trim | −24 … +24 dB | 0 | Output. |

### Display

Input spectrum, the estimated envelope, and the shifted envelope, on one plot,
with the correction curve `H` behind them. You can see the estimator refusing to
track harmonics, and you can see exactly what filter is being applied.

---

## Part 5 — What is deliberately not here

- **Polyphony.** The f0 and voicing model assumes one voice. Stated in the brief.
- **Pitch shifting.** A different problem with different tradeoffs, and the
  reason to build this was to have formant control without it.
- **Character, drive, saturation.** FrostyEQ is where color belongs. If a
  Little-AlterBoy-style drive is wanted later it goes after the shifter, not
  inside it, so the colorless claim stays testable.
- **Formant-preserving pitch shift.** Would need the resynthesis this design
  exists to avoid.

## Part 6 — Verification

The claims above are tests, not prose. `tests/DspTests.cpp` asserts:

- zero shift nulls against the delayed input with **exactly zero error**;
- Mix = 0 does the same;
- the reported latency is exactly the measured impulse delay;
- a synthetic vowel's formants land within a few percent of `α ×` their original
  frequencies;
- f0 is unchanged, to the resolution of the analysis, at every shift;
- a sustained sung note picks up no frame-rate modulation (−60 dB, against the
  −20 to −35 dB a phase vocoder produces on the same signal);
- no inter-harmonic energy is added at any shift (−79 to −94 dB across ±12
  semitones), which is the direct measurement of the artefact resynthesis makes
  and filtering cannot;
- white noise is classified unvoiced and a synthetic vowel voiced;
- the true envelope on a known synthetic vowel recovers the known filter;
- the correction filter's impulse response fits inside the zero-padding, which
  is what makes the frame maths exact.

`tools/measure` runs the same signals offline and prints the numbers.

## References

- Röbel & Rodet, *Efficient Spectral Envelope Estimation and its application to
  pitch shifting and envelope preservation*, DAFx-05 — the True Envelope method.
- Röbel, Villavicencio & Rodet, *On cepstral and all-pole based spectral
  envelope modeling with unknown model order*, Pattern Recognition Letters 2007.
- Noll, *Cepstrum pitch determination*, JASA 1967 — cepstral f0 and the peak
  prominence voicing measure.
- Soundtoys Little AlterBoy — the reference for what the control should feel
  like, and for the sound the artists above are actually making.
