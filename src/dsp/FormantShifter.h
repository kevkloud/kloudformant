#pragma once

#include "Fft.h"
#include "FormantWarp.h"
#include "SpectralEnvelope.h"
#include <atomic>
#include <cstdint>
#include <complex>
#include <vector>

namespace vellum
{

enum class Tracking { fast = 0, normal = 1, smooth = 2 };

/** Window length for each tracking setting, at 48 kHz. Rescaled to the host
    rate in prepare() so the *time* constants, not the sample counts, stay put. */
inline int nominalWindowFor (Tracking t) noexcept
{
    switch (t)
    {
        case Tracking::fast:   return 1024;
        case Tracking::smooth: return 4096;
        case Tracking::normal:
        default:               return 2048;
    }
}

//==============================================================================
/** One channel's formant shifter.

    ### What it does not do

    It does not resynthesize. There is no phase estimation, no peak picking, no
    oscillator bank, no overlap-add of a reconstructed signal. The input frame
    goes into the FFT and comes back out of the IFFT having been multiplied by a
    real, non-negative, smooth gain curve. Phase is untouched at every bin.

    That is the whole reason it can sound natural: the glottal pulse's phase
    coherence across harmonics -- the thing that makes a voice a voice rather
    than a buzz -- is exactly what a phase vocoder destroys and exactly what a
    filter cannot touch.

    ### The curve

        H(f) = E(g(f)) / E(f)

    E is the true envelope of *this* frame and g is the warp map. At zero shift
    g is the identity, so H is 1 at every bin no matter what E came out as: the
    estimator's errors cancel in the ratio. That is why colourlessness here is
    structural rather than something that had to be tuned, and why the null test
    in DspTests passes at the noise floor of double precision rather than at
    "inaudible".

    ### Why the FFT is twice the window

    Multiplying a spectrum by H is a *circular* convolution in the time domain.
    It equals the honest linear convolution only if the windowed frame plus H's
    impulse response fits inside the transform. The frame is L long. H is built
    from envelopes liftered to order p, so its impulse response is bounded by
    2p + 1 samples. Taking N = 2L and holding p < L/2 (SpectralEnvelope clamps
    to N/4 - 1, which is exactly that bound) makes the wrap-around term
    identically zero. Spectral processors that skip this eat the aliasing as a
    faint buzz that is loudest, annoyingly, on quiet material.
*/
class FormantShifter
{
public:
    struct Settings
    {
        double ratio = 1.0;             // alpha; 1.0 is no shift
        double lowPivotHz = 120.0;
        double highPivotHz = 6000.0;
        double consonantPreservation = 0.75;   // 0..1
        double orderFraction = 0.55;
        bool   levelMatch = true;
    };

    /** @param maxWindow the largest window any tracking setting will ask for at
        this rate, so switching tracking never allocates on the audio thread. */
    void prepare (double sampleRate, int window, int maxWindow);

    /** Changes the frame size in place. Only legal between blocks; the caller
        (DspCore) routes it through prepare() when the host is not running. */
    void setWindow (int window) noexcept;

    void reset() noexcept;

    void setSettings (const Settings& s) noexcept { settings = s; }

    /** Samples of delay this introduces, which the dry path must match and the
        host must be told about.

        An overlap-add accumulator slot is complete only once the last frame
        covering it has been added, and output is emitted at a constant rate, so
        the delay is set by the oldest sample in each emitted batch rather than
        the newest: window - 1, not window - hop. Measured, not assumed --
        DspTests drives an impulse through and asserts the peak lands exactly
        here, because a reported latency that is off by even one sample puts the
        host's delay compensation out and quietly breaks every null test above. */
    int getLatencySamples() const noexcept { return window - 1; }

    int getWindow() const noexcept { return window; }
    int getHop() const noexcept    { return hop; }

    /** In-place. Any block size, including sizes that straddle frame
        boundaries; the class carries its own input and output rings. */
    void process (float* samples, int numSamples) noexcept;

    //== Display feed ==========================================================
    // Written by the audio thread at frame rate, read by the editor on a timer.
    // Publish-and-sample: the reader can tear across a frame boundary and the
    // worst case is one repainted frame that mixes two analyses, which is
    // invisible at 30 Hz. Nothing here is pushed from audio to UI.
    struct DisplayFrame
    {
        std::vector<float> inputDb, envelopeDb, shiftedDb, correctionDb;
        double f0Hz = 0.0;
        double voicedness = 0.0;
        double binWidthHz = 1.0;
        int    numBins = 0;
    };

    /** Copies the most recent analysis out. Safe to call from any thread. */
    void copyDisplayFrame (DisplayFrame&) const;

private:
    void processFrame() noexcept;
    void buildCorrection (const FrameAnalysis&) noexcept;

    /** Force the correction curve's impulse response inside the zero-padding.

        The curve is built by exponentiating a difference of liftered log
        envelopes, and an exponential of a band-limited function is not itself
        band-limited -- it spreads, and the spread is what wraps around in the
        circular convolution. Liftering the linear gain curve to an order the
        padding can hold makes the bound hold by construction rather than by
        argument, which is the difference between a claim and a guarantee. */
    void bandLimitCorrection() noexcept;

    /** Longest impulse response the zero-padding can carry without wrap-around:
        the response is symmetric about zero, so it gets half the budget on each
        side of (N - L). */
    int maxFilterOrder() const noexcept { return (fftSize - window) / 2 - 1; }

    Fft fft;
    SpectralEnvelope envelopeEstimator;
    FormantWarp warp;
    Settings settings;

    double sampleRate = 48000.0;
    int window = 2048;
    int fftSize = 4096;
    int hop = 512;
    int numBins = 2049;

    // Both double. These multiply every sample twice on the way through, so in
    // float their own rounding -- not the signal's -- sets the transparency
    // floor, about 6 dB above what the output format can express.
    std::vector<double> analysisWindow;   // Hann, also used for synthesis
    std::vector<double> windowSum;        // measured overlap sum, divided out

    std::vector<float>  inputRing;

    // The overlap-add accumulator is double. Four float additions per output
    // sample would put the null test's floor around -135 dB; accumulating in
    // double leaves the single rounding at the final store, which is the -144 dB
    // that 32-bit output can express at all.
    std::vector<double> outputRing;
    int ringWrite = 0;
    int samplesUntilFrame = 0;

    std::vector<std::complex<double>> spectrum;
    std::vector<double> frame;
    std::vector<float> envelopeDb;
    std::vector<float> shiftedDb;
    std::vector<float> correctionGain;
    std::vector<float> targetGain;
    std::vector<std::complex<double>> filterScratch;

    // Broadband compensation, one-pole smoothed across frames so it cannot
    // pump on a plosive.
    float levelMatchGain = 1.0f;

    // The published display frame, plus a sequence counter so the reader can
    // tell it got a torn copy. Cheap and honest; a lock would be neither.
    mutable DisplayFrame display;
    std::atomic<uint32_t> displaySequence { 0 };

    double lastRatio = 1.0, lastLowPivot = 0.0, lastHighPivot = 0.0;
};

} // namespace vellum
