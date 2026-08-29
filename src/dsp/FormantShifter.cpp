#include "FormantShifter.h"

#include <algorithm>
#include <cmath>

namespace kloudformant
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    /** The correction filter is a ratio of two envelope estimates, so at the
        very edges of the band -- where one of them is extrapolated -- it can ask
        for an unreasonable amount of gain. Nothing musical needs more than this,
        and the clamp also bounds the impulse response. */
    constexpr float kMaxCorrectionDb = 24.0f;

    /** Per-frame one-pole on the correction curve. At a 512-sample hop this is
        a time constant near 15 ms -- faster than a vowel transition, slower
        than the analysis noise it is there to remove. */
    constexpr float kGainSmoothing = 0.25f;

    float interpolate (const std::vector<float>& curve, float position) noexcept
    {
        const auto last = (int) curve.size() - 1;
        const auto i = (int) position;

        if (i >= last)
            return curve[(size_t) last];

        const auto frac = position - (float) i;
        return curve[(size_t) i] + frac * (curve[(size_t) (i + 1)] - curve[(size_t) i]);
    }
}

//==============================================================================
void FormantShifter::prepare (double rate, int windowSize, int maxWindow)
{
    sampleRate = rate;

    const auto maxFft = maxWindow * 2;

    // Sized for the largest window any tracking setting can select, so changing
    // tracking never reallocates.
    inputRing.assign  ((size_t) maxWindow, 0.0f);
    outputRing.assign ((size_t) maxWindow, 0.0);
    frame.assign      ((size_t) maxWindow, 0.0);
    analysisWindow.assign ((size_t) maxWindow, 0.0);
    spectrum.assign   ((size_t) maxFft, { 0.0, 0.0 });
    envelopeDb.assign ((size_t) (maxFft / 2 + 1), 0.0f);
    shiftedDb.assign  ((size_t) (maxFft / 2 + 1), 0.0f);
    correctionGain.assign ((size_t) (maxFft / 2 + 1), 1.0f);
    targetGain.assign     ((size_t) (maxFft / 2 + 1), 1.0f);
    filterScratch.assign  ((size_t) maxFft, { 0.0, 0.0 });

    setWindow (windowSize);
}

void FormantShifter::setWindow (int windowSize) noexcept
{
    window  = windowSize;
    fftSize = windowSize * 2;      // see the class comment: this is what makes
    hop     = windowSize / 4;      // the frame maths exact, not a nicety
    numBins = fftSize / 2 + 1;

    fft.prepare (fftSize);
    envelopeEstimator.prepare (fftSize, sampleRate);
    warp.prepare (numBins, sampleRate / (double) fftSize);

    // Periodic Hann, used for both analysis and synthesis. Hann-squared at 75 %
    // overlap sums to a constant 1.5; measuring it rather than hard-coding it
    // keeps the reconstruction exact if the hop ratio is ever changed.
    for (int i = 0; i < window; ++i)
        analysisWindow[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) window);

    windowSum.assign ((size_t) hop, 0.0);

    for (int n = 0; n < hop; ++n)
        for (int k = 0; k * hop + n < window; ++k)
        {
            const auto w = analysisWindow[(size_t) (k * hop + n)];
            windowSum[(size_t) n] += w * w;
        }

    for (auto& s : windowSum)
        s = s > 1.0e-9 ? 1.0 / s : 0.0;

    // The display's buffers are sized here too, on the message thread's side of
    // a prepare(), never on the audio thread.
    for (auto* buffer : { &display.inputDb, &display.envelopeDb,
                          &display.shiftedDb, &display.correctionDb })
        buffer->assign ((size_t) numBins, -120.0f);

    display.numBins = numBins;
    display.binWidthHz = sampleRate / (double) fftSize;

    reset();
}

void FormantShifter::reset() noexcept
{
    std::fill (inputRing.begin(),  inputRing.end(),  0.0f);
    std::fill (outputRing.begin(), outputRing.end(), 0.0);

    ringWrite = 0;
    samplesUntilFrame = hop;
    levelMatchGain = 1.0f;

    std::fill (correctionGain.begin(), correctionGain.end(), 1.0f);

    envelopeEstimator.reset();

    lastRatio = lastLowPivot = lastHighPivot = 0.0;
}

//==============================================================================
void FormantShifter::process (float* samples, int numSamples) noexcept
{
    for (int n = 0; n < numSamples; ++n)
    {
        // The input ring holds the most recent `window` samples with the oldest
        // at ringWrite; the output ring is the overlap-add accumulator, read
        // from and cleared at the same position.
        inputRing[(size_t) ringWrite] = samples[n];

        if (--samplesUntilFrame == 0)
        {
            samplesUntilFrame = hop;
            processFrame();
        }

        // Read the *oldest* slot, not the one just written. This frame put its
        // last contribution into it, so it is now complete; the newest slot has
        // received one of its four overlaps and is not. That distinction is the
        // whole latency of the plugin: window - 1 samples.
        const auto readIndex = ringWrite + 1 < window ? ringWrite + 1 : 0;

        samples[n] = (float) outputRing[(size_t) readIndex];
        outputRing[(size_t) readIndex] = 0.0;

        ringWrite = readIndex;
    }
}

void FormantShifter::processFrame() noexcept
{
    // De-rotate the ring into a linear frame, oldest first, windowed.
    for (int i = 0; i < window; ++i)
    {
        const auto src = ringWrite + 1 + i;
        frame[(size_t) i] = (double) inputRing[(size_t) (src < window ? src : src - window)]
                          * analysisWindow[(size_t) i];
    }

    fft.forwardReal (frame.data(), window, spectrum.data());

    const auto analysis = envelopeEstimator.analyse (spectrum.data(), envelopeDb.data(),
                                                     settings.orderFraction);

    buildCorrection (analysis);

    //== Apply, as a real gain per bin. Phase is never read and never written. ==
    double energyIn = 0.0, energyOut = 0.0;

    for (int k = 0; k < numBins; ++k)
    {
        const auto g = correctionGain[(size_t) k];
        const auto weight = (k == 0 || k == fftSize / 2) ? 1.0 : 2.0;
        const auto magSq = std::norm (spectrum[(size_t) k]);

        display.inputDb[(size_t) k] = (float) (10.0 * std::log10 (magSq + 1.0e-30));

        energyIn  += weight * magSq;
        energyOut += weight * magSq * (double) g * (double) g;

        spectrum[(size_t) k] *= (double) g;

        if (k > 0 && k < fftSize / 2)
            spectrum[(size_t) (fftSize - k)] *= (double) g;
    }

    if (settings.levelMatch && energyOut > 1.0e-30 && energyIn > 1.0e-30)
    {
        // Compensate against the signal's own spectrum, not a flat assumption:
        // what matters is the loudness change *this* voice experiences, which
        // depends on where it has energy.
        const auto target = (float) std::clamp (std::sqrt (energyIn / energyOut), 0.25, 4.0);
        levelMatchGain += 0.3f * (target - levelMatchGain);
    }
    else
    {
        levelMatchGain += 0.3f * (1.0f - levelMatchGain);
    }

    fft.inverse (spectrum.data());

    // Overlap-add. The synthesis window is the same Hann; the running overlap
    // sum measured in setWindow() is divided out so the reconstruction is unity.
    const auto gain = settings.levelMatch ? levelMatchGain : 1.0f;

    for (int i = 0; i < window; ++i)
    {
        const auto dst = ringWrite + 1 + i;
        const auto index = dst < window ? dst : dst - window;

        outputRing[(size_t) index] += spectrum[(size_t) i].real()
                                    * analysisWindow[(size_t) i]
                                    * windowSum[(size_t) (i % hop)]
                                    * (double) gain;
    }

    //== Publish for the display ===============================================
    std::copy (envelopeDb.begin(), envelopeDb.begin() + numBins, display.envelopeDb.begin());
    std::copy (shiftedDb.begin(),  shiftedDb.begin()  + numBins, display.shiftedDb.begin());

    for (int k = 0; k < numBins; ++k)
        display.correctionDb[(size_t) k] = 20.0f * std::log10 (std::max (correctionGain[(size_t) k], 1.0e-6f));

    display.f0Hz = analysis.f0Hz;
    display.voicedness = analysis.voicedness;
    displaySequence.fetch_add (1, std::memory_order_release);
}

void FormantShifter::buildCorrection (const FrameAnalysis& analysis) noexcept
{
    if (settings.ratio != lastRatio
        || settings.lowPivotHz != lastLowPivot
        || settings.highPivotHz != lastHighPivot)
    {
        warp.build (settings.ratio, settings.lowPivotHz, settings.highPivotHz);

        lastRatio     = settings.ratio;
        lastLowPivot  = settings.lowPivotHz;
        lastHighPivot = settings.highPivotHz;
    }

    // Unvoiced frames are noise shaped by a constriction at the front of the
    // mouth, not by the whole tract, so scaling them by the vowel ratio is what
    // makes sibilants lisp. Backing the correction off in dB rather than
    // rebuilding the warp per frame is exact at both ends -- 0 dB is 0 dB -- and
    // keeps the identity fast path intact.
    const auto depth = (float) (1.0 - settings.consonantPreservation * (1.0 - analysis.voicedness));

    if (warp.isIdentity() || depth <= 0.0f)
    {
        std::fill (targetGain.begin(), targetGain.begin() + numBins, 1.0f);
        std::copy (envelopeDb.begin(), envelopeDb.begin() + numBins, shiftedDb.begin());
    }
    else
    {
        for (int k = 0; k < numBins; ++k)
        {
            shiftedDb[(size_t) k] = interpolate (envelopeDb, warp.sourceBinFor (k));

            const auto correctionDb = std::clamp ((shiftedDb[(size_t) k] - envelopeDb[(size_t) k]) * depth,
                                                  -kMaxCorrectionDb, kMaxCorrectionDb);

            targetGain[(size_t) k] = std::pow (10.0f, correctionDb * 0.05f);
        }

        bandLimitCorrection();
    }

    // Smooth across frames. A vocal tract does not reshape itself in 11 ms, so
    // any frame-to-frame movement in the curve beyond that is analysis noise --
    // and because it moves at exactly the hop rate, it lands as sidebands
    // around every partial. Smoothing here is what keeps a sustained note from
    // acquiring the faint tremolo that gives most spectral processors away.
    //
    // The snap matters: without it a settled control would leave the gains a
    // few ULPs off unity forever and the transparency claim would be
    // approximate rather than exact.
    for (int k = 0; k < numBins; ++k)
    {
        auto& g = correctionGain[(size_t) k];
        const auto target = targetGain[(size_t) k];

        g += kGainSmoothing * (target - g);

        if (std::abs (target - g) < 1.0e-7f)
            g = target;
    }
}

void FormantShifter::bandLimitCorrection() noexcept
{
    const auto order = maxFilterOrder();

    for (int k = 0; k < numBins; ++k)
    {
        filterScratch[(size_t) k] = { (double) targetGain[(size_t) k], 0.0 };

        if (k > 0 && k < fftSize / 2)
            filterScratch[(size_t) (fftSize - k)] = { (double) targetGain[(size_t) k], 0.0 };
    }

    fft.inverse (filterScratch.data());

    for (int n = order + 1; n < fftSize - order; ++n)
        filterScratch[(size_t) n] = { 0.0, 0.0 };

    fft.forward (filterScratch.data());

    // Truncating the response can in principle ring the curve slightly negative
    // where it was steep. A gain that changes sign would invert a band, so the
    // floor is a hard requirement rather than defensive coding; at 0.05 (-26 dB)
    // it sits below the +/-24 dB the curve is already clamped to and so never
    // bites on anything the shifter actually asks for.
    for (int k = 0; k < numBins; ++k)
        targetGain[(size_t) k] = std::max (0.05f, (float) filterScratch[(size_t) k].real());
}

//==============================================================================
void FormantShifter::copyDisplayFrame (DisplayFrame& out) const
{
    // Publish-and-sample. A reader can in principle catch a frame boundary and
    // get a curve stitched from two analyses; at a 30 Hz repaint against a
    // ~90 Hz frame rate that is one slightly-wrong pixel column, and the
    // alternative -- a lock shared with the audio thread -- is not on the table.
    out.numBins = display.numBins;
    out.binWidthHz = display.binWidthHz;
    out.f0Hz = display.f0Hz;
    out.voicedness = display.voicedness;

    const auto n = (size_t) display.numBins;

    if (out.inputDb.size() != n)
    {
        out.inputDb.resize (n);
        out.envelopeDb.resize (n);
        out.shiftedDb.resize (n);
        out.correctionDb.resize (n);
    }

    std::copy_n (display.inputDb.begin(),      n, out.inputDb.begin());
    std::copy_n (display.envelopeDb.begin(),   n, out.envelopeDb.begin());
    std::copy_n (display.shiftedDb.begin(),    n, out.shiftedDb.begin());
    std::copy_n (display.correctionDb.begin(), n, out.correctionDb.begin());
}

} // namespace kloudformant
