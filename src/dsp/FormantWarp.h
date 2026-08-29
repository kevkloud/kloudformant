#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace vellum
{

/** The frequency map that says, for each output bin, where in the *original*
    envelope to read from.

    A naive formant shift is g(f) = f / alpha across the whole spectrum. That is
    wrong at both ends and it is why most shifters change the weight of a voice
    as well as its size:

    - **At the bottom**, the envelope is steep below the first formant, so
      sliding it there changes the gain applied to the fundamental itself by
      many dB. The voice gets boomy shifting down and thin shifting up, and low
      notes get a different treatment from high ones. Nothing below the
      fundamental is vocal-tract resonance anyway, so there is nothing there to
      shift.

    - **At the top**, above roughly 5-6 kHz you are no longer looking at tract
      resonances but at the source's own spectral tilt, the radiation
      characteristic, and -- on consonants -- noise shaped by a constriction at
      the front of the mouth. Scaling that region tilts the whole top end and
      turns sibilants lispy.

    So the ratio is ramped in and out in log-frequency and the map is built by

        s(f) = ln(alpha) * w(f)
        g(f) = f * exp(-s(f))

    with w a raised cosine that is 0 below the low pivot, 1 across the formant
    band, and 0 again above the high pivot. Outside the band g(f) == f exactly,
    so the envelope there is read from where it already was and the correction
    filter is 0 dB.

    Held as a table of source *bin* positions so the hot loop does a lookup and
    a lerp rather than an exp per bin per frame.
*/
class FormantWarp
{
public:
    void prepare (int numBins, double binWidthHz)
    {
        bins = numBins;
        binHz = binWidthHz;
        sourceBin.assign ((size_t) numBins, 0.0f);
        identity = true;
    }

    /** True when the last build produced g(f) == f everywhere, which means the
        correction filter is exactly 0 dB and the whole frame can be skipped. */
    bool isIdentity() const noexcept { return identity; }

    float sourceBinFor (int k) const noexcept { return sourceBin[(size_t) k]; }

    /** @param ratio      alpha. 1.0 is no shift; >1 moves formants up.
        @param lowPivotHz below this the envelope does not move at all.
        @param highPivotHz above this the shift tapers back out.
    */
    void build (double ratio, double lowPivotHz, double highPivotHz) noexcept
    {
        identity = std::abs (ratio - 1.0) < 1.0e-9;

        if (identity)
        {
            for (int k = 0; k < bins; ++k)
                sourceBin[(size_t) k] = (float) k;

            return;
        }

        const auto logAlpha = std::log (ratio);

        // The ramps are an octave wide on each side. Wide enough that the
        // correction filter stays smooth -- a sharp edge in w would put a sharp
        // edge in H, lengthen its impulse response, and undo the zero-padding
        // argument that makes the frame maths exact.
        const auto lowStart  = std::max (10.0, lowPivotHz * 0.5);
        const auto lowEnd    = std::max (lowStart * 1.001, lowPivotHz);
        const auto highStart = std::max (lowEnd * 1.001, highPivotHz);
        const auto highEnd   = highStart * 2.0;

        for (int k = 0; k < bins; ++k)
        {
            const auto f = (double) k * binHz;
            const auto w = weight (f, lowStart, lowEnd, highStart, highEnd);
            const auto g = f * std::exp (-logAlpha * w);

            sourceBin[(size_t) k] = (float) std::clamp (g / binHz, 0.0, (double) (bins - 1));
        }

        // A ramp plus a large ratio can in principle fold the map back on
        // itself, which would read the envelope backwards over a few bins and
        // put a notch where there is no notch. Enforcing monotonicity costs one
        // pass and removes the failure mode entirely.
        for (int k = 1; k < bins; ++k)
            sourceBin[(size_t) k] = std::max (sourceBin[(size_t) k], sourceBin[(size_t) (k - 1)]);
    }

private:
    /** 0 below lowStart, rising to 1 by lowEnd, holding, falling back to 0 by
        highEnd. Raised cosine in log-frequency, so the ramps look symmetric to
        the ear rather than to the axis. */
    static double weight (double f, double lowStart, double lowEnd,
                          double highStart, double highEnd) noexcept
    {
        if (f <= lowStart || f >= highEnd)
            return 0.0;

        if (f >= lowEnd && f <= highStart)
            return 1.0;

        const auto raisedCosine = [] (double x)
        {
            return 0.5 - 0.5 * std::cos (3.14159265358979323846 * std::clamp (x, 0.0, 1.0));
        };

        if (f < lowEnd)
            return raisedCosine (std::log (f / lowStart) / std::log (lowEnd / lowStart));

        return raisedCosine (1.0 - std::log (f / highStart) / std::log (highEnd / highStart));
    }

    std::vector<float> sourceBin;
    int bins = 0;
    double binHz = 1.0;
    bool identity = true;
};

} // namespace vellum
