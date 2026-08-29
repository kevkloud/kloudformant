#include "SpectralEnvelope.h"

#include <algorithm>
#include <cmath>

namespace kloudformant
{

namespace
{
    // log|X| is unbounded below, and a silent bin would otherwise poison the
    // whole cepstrum. -200 dB in nepers.
    constexpr double kLogFloor = -23.02585092994046;

    constexpr double kNepersToDb = 8.685889638065035;   // 20 / ln(10)
}

void SpectralEnvelope::prepare (int size, double rate)
{
    fftSize    = size;
    numBins    = size / 2 + 1;
    sampleRate = rate;

    fft.prepare (fftSize);

    logMag.assign   ((size_t) fftSize, 0.0);
    envelope.assign ((size_t) fftSize, 0.0);
    work.assign     ((size_t) fftSize, { 0.0, 0.0 });

    reset();
}

void SpectralEnvelope::reset() noexcept
{
    recentF0.fill (0.0);
    recentWrite   = 0;
    historyPrimed = false;

    std::fill (envelope.begin(), envelope.end(), 0.0);
}

//==============================================================================
void SpectralEnvelope::lifter (int order) noexcept
{
    // envelope[] holds a log spectrum on entry; leave it holding the liftered
    // one. The log spectrum is real and even, so its cepstrum is real and even
    // too -- the imaginary parts are rounding noise and are discarded.
    for (int i = 0; i < fftSize; ++i)
        work[(size_t) i] = { envelope[(size_t) i], 0.0 };

    fft.inverse (work.data());

    // Keep quefrencies 0..order and their mirror at the top; zero the middle.
    for (int i = order + 1; i < fftSize - order; ++i)
        work[(size_t) i] = { 0.0, 0.0 };

    fft.forward (work.data());

    for (int i = 0; i < fftSize; ++i)
        envelope[(size_t) i] = work[(size_t) i].real();
}

double SpectralEnvelope::findPitchPeriod (double& prominenceOut) noexcept
{
    prominenceOut = 0.0;

    // Copy the log spectrum and transform. Removing the mean first keeps the
    // (huge) c[0] term from leaking into the search window through the
    // transform's own sidelobes.
    double mean = 0.0;

    for (int i = 0; i < fftSize; ++i)
        mean += logMag[(size_t) i];

    mean /= (double) fftSize;

    auto& scratch = work;

    for (int i = 0; i < fftSize; ++i)
        scratch[(size_t) i] = { logMag[(size_t) i] - mean, 0.0 };

    fft.inverse (scratch.data());

    const int lo = std::max (2, (int) std::floor (sampleRate / kMaxF0));
    const int hi = std::min (fftSize / 2 - 1, (int) std::ceil (sampleRate / kMinF0));

    if (hi <= lo)
        return 0.0;

    int    bestIndex = 0;
    double bestValue = 0.0;
    double sum = 0.0;

    for (int q = lo; q <= hi; ++q)
    {
        const auto v = scratch[(size_t) q].real();
        sum += v;

        if (v > bestValue)
        {
            bestValue = v;
            bestIndex = q;
        }
    }

    if (bestIndex == 0)
        return 0.0;

    // Prominence is the peak measured against the mean of the search window,
    // normalised by the window's own spread. A periodic frame puts a sharp
    // isolated spike here; noise puts up a flat field of similar values.
    const auto windowMean = sum / (double) (hi - lo + 1);
    double variance = 0.0;

    for (int q = lo; q <= hi; ++q)
    {
        const auto d = scratch[(size_t) q].real() - windowMean;
        variance += d * d;
    }

    const auto deviation = std::sqrt (variance / (double) (hi - lo + 1));

    if (deviation > 1.0e-12)
        prominenceOut = (bestValue - windowMean) / deviation;

    // Parabolic interpolation across the peak, so f0 is not quantised to whole
    // samples of period -- at 48 kHz one sample of period is 4 Hz at 440 Hz,
    // which would show up as a visible step in the chosen lifter order.
    double period = (double) bestIndex;

    if (bestIndex > lo && bestIndex < hi)
    {
        const auto a = scratch[(size_t) (bestIndex - 1)].real();
        const auto b = scratch[(size_t) bestIndex].real();
        const auto c = scratch[(size_t) (bestIndex + 1)].real();
        const auto denom = a - 2.0 * b + c;

        if (std::abs (denom) > 1.0e-12)
            period += 0.5 * (a - c) / denom;
    }

    return period;
}

//==============================================================================
FrameAnalysis SpectralEnvelope::analyse (const std::complex<double>* spectrum,
                                         float* envelopeDb,
                                         double orderFraction) noexcept
{
    FrameAnalysis result;

    for (int i = 0; i < fftSize; ++i)
    {
        const auto magSq = std::norm (spectrum[i]);
        logMag[(size_t) i] = std::max (0.5 * std::log (magSq + 1.0e-300), kLogFloor);
    }

    //== Pitch and voicing =====================================================
    double prominence = 0.0;
    const auto period = findPitchPeriod (prominence);

    // Prominence in standard deviations. Below ~4 the peak is indistinguishable
    // from the field around it; above ~9 the frame is unambiguously periodic.
    result.voicedness = std::clamp ((prominence - 4.0) / 5.0, 0.0, 1.0);

    double f0 = period > 0.0 ? sampleRate / period : 0.0;

    if (result.voicedness > 0.0 && f0 > 0.0)
    {
        recentF0[(size_t) recentWrite] = f0;
        recentWrite = (recentWrite + 1) % (int) recentF0.size();

        if (! historyPrimed)
        {
            recentF0.fill (f0);
            historyPrimed = true;
        }

        auto sorted = recentF0;
        std::sort (sorted.begin(), sorted.end());
        f0 = sorted[1];
    }
    else
    {
        historyPrimed = false;
    }

    result.f0Hz = f0;

    //== Lifter order ==========================================================
    // The harmonic comb lives at quefrency index sampleRate/f0 -- the pitch
    // period in samples. Staying a fixed fraction below it is what makes the
    // envelope structurally unable to track partials.
    int order;

    if (result.voicedness > 0.0 && f0 > 0.0)
        order = (int) std::lround (std::clamp (orderFraction, 0.1, 0.95) * sampleRate / f0);
    else
        order = (int) std::lround (sampleRate / kUnvoicedResolutionHz);

    // Above fftSize/4 the kept band and its mirror would meet in the middle and
    // the lifter would stop being a lifter. That bound also happens to be
    // exactly what keeps the correction filter's impulse response inside the
    // zero-padding -- see FormantShifter.
    order = std::clamp (order, 4, fftSize / 4 - 1);
    result.cepstralOrder = order;

    //== True envelope =========================================================
    envelope = logMag;
    lifter (order);

    for (int iteration = 0; iteration < kMaxIterations; ++iteration)
    {
        double maxUpdate = 0.0;

        for (int i = 0; i < fftSize; ++i)
        {
            const auto v = logMag[(size_t) i];
            auto& c = envelope[(size_t) i];

            if (v > c)
            {
                maxUpdate = std::max (maxUpdate, v - c);
                c = v;
            }
        }

        // Once no bin pokes more than half a dB above the curve, the remaining
        // passes only move it by less than the ratio can express.
        if (maxUpdate * kNepersToDb < kConvergenceDb)
            break;

        lifter (order);
    }

    for (int k = 0; k < numBins; ++k)
        envelopeDb[k] = (float) (envelope[(size_t) k] * kNepersToDb);

    return result;
}

} // namespace kloudformant
