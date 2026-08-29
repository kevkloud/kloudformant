#pragma once

#include "Fft.h"
#include <array>
#include <complex>
#include <vector>

namespace vellum
{

/** What one frame's analysis produced. */
struct FrameAnalysis
{
    double f0Hz = 0.0;          // 0 when nothing periodic was found
    double voicedness = 0.0;    // 0 unvoiced .. 1 clearly periodic
    int    cepstralOrder = 0;   // lifter order actually used for the envelope
};

//==============================================================================
/** True Envelope estimation, with the pitch and voicing measure that the
    envelope's own order depends on.

    All three come out of a single real cepstrum, which is not a coincidence:
    the harmonic comb in the log spectrum has period f0, so it shows up in the
    cepstrum as one peak at the pitch period. Its position is f0, its height is
    how periodic the frame is, and the lifter order that removes it is the
    largest order the envelope is allowed to use.

    The envelope itself is Roebel & Rodet's iteration

        C <- lifter_p( max(V, C) )

    started from lifter_p(V). Taking the running maximum against the original
    log spectrum means each pass can fill the valleys between harmonics but can
    never be pulled down into them, so the result converges on the upper
    envelope through the harmonic peaks. A plain cepstral smoothing -- one pass,
    no max -- sits in the middle of the ripple instead and under-reads every
    formant by several dB.

    Everything is in log magnitude (natural log, converted at the edges), and
    every buffer is sized in prepare(). Nothing here allocates once running.
*/
class SpectralEnvelope
{
public:
    void prepare (int fftSize, double sampleRate);

    /** Analyse one already-transformed frame.

        `spectrum` is the full complex FFT output of length fftSize. Writes the
        log-magnitude envelope, in dB, into `envelopeDb` (fftSize/2 + 1 bins)
        and returns the pitch and voicing it measured on the way.

        `orderFraction` is the *Resolution* control: the cepstral order as a
        fraction of the measured pitch period. Below 1.0 by construction, so
        the envelope cannot resolve the harmonic spacing however hard it tries.
    */
    FrameAnalysis analyse (const std::complex<double>* spectrum,
                           float* envelopeDb,
                           double orderFraction) noexcept;

    void reset() noexcept;

    /** Pitch search range. Wide enough for a bass talking and a soprano
        belting, narrow enough that the cepstral peak search does not wander
        into the low-quefrency region where the envelope itself lives. */
    static constexpr double kMinF0 = 60.0;
    static constexpr double kMaxF0 = 1000.0;

    /** Order used when the frame is unvoiced and there is no pitch period to
        scale against: roughly 250 Hz of spectral resolution, which resolves
        fricative shapes without chasing noise. */
    static constexpr double kUnvoicedResolutionHz = 250.0;

private:
    /** Keep cepstral coefficients 0..order (and their mirror), zero the rest,
        and transform back to a log spectrum. */
    void lifter (int order) noexcept;

    /** Cepstral peak picking. Returns the period in samples, or 0. */
    double findPitchPeriod (double& prominenceOut) noexcept;

    Fft fft;
    int fftSize = 0;
    int numBins = 0;
    double sampleRate = 48000.0;

    std::vector<double> logMag;      // V, full length, symmetric
    std::vector<double> envelope;    // C, full length, symmetric
    std::vector<std::complex<double>> work;

    // Recent pitch estimates, median-filtered. A single frame's cepstrum
    // occasionally picks the second harmonic and reports the octave; a median
    // of three throws that away without adding meaningful lag.
    std::array<double, 3> recentF0 { };
    int recentWrite = 0;
    bool historyPrimed = false;

    static constexpr int kMaxIterations = 8;
    static constexpr double kConvergenceDb = 0.5;
};

} // namespace vellum
