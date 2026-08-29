# Vellum

A formant shifter for monophonic vocals that is transparent when you are not
using it, and does not sound robotic when you are. VST3 / AU / Standalone.

Formant only. The pitch is never touched.

## Status

DSP core complete and tested. Plugin wrapper and panel in progress.

## The idea

Most formant shifters take the signal apart and put it back together — analyse,
modify, resynthesize. The rebuilding is what makes them sound synthetic, and it
happens at every setting, including no shift at all.

Vellum never resynthesizes. It estimates the spectral envelope, works out the
difference between that envelope and where the envelope *would* be if the vocal
tract were longer or shorter, and applies that difference to the untouched input
as a gentle, slowly-varying EQ curve. Phase is never read and never written. The
glottal excitation, the transients, the breath, the room and the harmonic phase
coherence that makes a voice sound like a voice all pass through unmodified.

Two things fall out of that:

**At zero shift the plugin is bit-transparent.** The correction curve is a ratio
of the envelope to itself, which is 1 at every bin no matter what the estimator
produced — so the estimator's errors cancel rather than accumulate. The output
samples equal the input samples. Not "below the noise floor": equal.

**The window length no longer smears the signal.** In a phase vocoder a long
window smears real transients, so frequency resolution trades against consonant
clarity. Here the window only sets how fast the *filter* tracks; the consonant
underneath it is untouched. That buys a window long enough to resolve a male
fundamental without the usual cost.

## Measured

From `tests/DspTests.cpp`, which asserts every number below.

| | |
|---|---|
| Null at zero shift | **exactly zero error** — bitwise identical to the delayed input |
| Inter-harmonic energy added | −79 to −94 dB across ±12 semitones |
| Frame-rate modulation, sustained vowel | −60 dB (a phase vocoder: −20 to −35 dB) |
| Formant placement accuracy | F2/F3 within 1–4 %, F1 within 10 % |
| Harmonic comb surviving in the envelope | −175 dB |
| Correction filter energy outside the zero-padding | −149 dB |
| Level change with Level Match on | 0.4 dB |

The formant figures are measured against a synthetic vowel — a bandlimited
glottal pulse train through resonators at known frequencies — so "correct" is
defined rather than guessed at, and the shift is asserted as the *ratio* between
the measured input and output positions so that any bias in the peak finder
cancels out.

## Why other formant shifters sound robotic

Six causes, each of which is a design decision here. The long version, with the
research behind it, is in [`docs/plan.md`](docs/plan.md).

1. **Resynthesis** destroys the phase coherence across harmonics that makes a
   glottal pulse a pulse. → Vellum filters instead.
2. **Envelope estimators that track harmonics** drag partials around relative to
   each other. → True Envelope, with the cepstral order locked below the pitch
   period so tracking partials is impossible rather than unlikely.
3. **Scaling the whole spectrum** drags the glottal tilt and the radiation
   characteristic along with the formants. → only a band is warped.
4. **Dragging the region around the fundamental** changes the voice's weight
   with the knob. → a low pivot, below which nothing moves.
5. **Treating consonants like vowels** makes sibilants lisp. → per-frame voicing
   measure, and the shift backs off on unvoiced frames.
6. **No loudness compensation** means louder wins the A/B. → the broadband gain
   the filter would apply to *this* signal is taken back out.

## Controls

| Control | Range | Default |
|---|---|---|
| Shift | −12 … +12 st | 0 |
| Low Pivot | 20 … 500 Hz | 120 Hz |
| High Pivot | 2 … 20 kHz | 6 kHz |
| Consonants | 0 … 100 % | 75 % |
| Resolution | 0.3 … 0.9 | 0.55 |
| Tracking | Fast / Normal / Smooth | Normal |
| Level Match | on / off | on |
| Mix | 0 … 100 % | 100 % |
| Trim | −24 … +24 dB | 0 |

Latency is `window − 1` — 21.3 ms Fast, 42.6 ms Normal, 85.3 ms Smooth at
48 kHz — reported to the host for delay compensation, and asserted against a
measured impulse in the tests.

## Building

```bash
git clone --recursive https://github.com/kevkloud/vellum-formant
cd vellum-formant
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The DSP core has no JUCE dependency, so the tests and the measurement harness
build and run without an audio framework — useful in CI, and the reason the
numbers above can be regenerated anywhere:

```bash
cmake -B build-dsp -DVELLUM_DSP_ONLY=ON
cmake --build build-dsp --parallel
./build-dsp/VellumDspTests
```

## Licence

AGPL-3.0. Not affiliated with any of the products named in `docs/plan.md`.
