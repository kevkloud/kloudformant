#pragma once

#include "FormantShifter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace vellum
{

/** One-pole parameter smoother.

    Snaps to the target once it is within epsilon, so a settled parameter
    compares exactly equal -- which is what lets the shifter recognise a ratio
    of exactly 1.0 and take the identity path.
*/
class Smoother
{
public:
    void prepare (double controlRateHz, double timeMs) noexcept
    {
        const auto tau = std::max (timeMs, 0.01) * 0.001;
        coeff = (float) (1.0 - std::exp (-1.0 / (std::max (controlRateHz, 1.0) * tau)));
    }

    void snap (float v) noexcept        { current = target = v; }
    void setTarget (float t) noexcept   { target = t; }
    float value() const noexcept        { return current; }

    float tick() noexcept
    {
        current += coeff * (target - current);

        if (std::abs (target - current) < 1.0e-6f)
            current = target;

        return current;
    }

private:
    float coeff = 1.0f, current = 0.0f, target = 0.0f;
};

//==============================================================================
/** Everything the plugin does to audio, with no dependency on JUCE's plugin
    layer or on a host. Takes plain values and raw buffers, so the measurement
    harness and the unit tests drive the real signal path directly.

    The dry path is delayed to match the shifter exactly, so Mix is a true
    crossfade between the input and a filtered copy of the *same* samples. An
    undelayed dry path would comb-filter against the wet one at every
    intermediate setting, which is a common and very audible tell.
*/
class DspCore
{
public:
    struct Params
    {
        float shiftSemitones = 0.0f;
        float lowPivotHz     = 120.0f;
        float highPivotHz    = 6000.0f;
        float consonants     = 75.0f;    // percent
        float resolution     = 0.55f;
        float mixPercent     = 100.0f;
        float trimDb         = 0.0f;
        bool  levelMatch     = true;
        bool  bypass         = false;
        Tracking tracking    = Tracking::normal;
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;

    /** Round-trip delay, in samples. Reported to the host so plugin delay
        compensation can undo it. */
    int getLatencySamples() const noexcept { return latencySamples; }

    /** Called once per block, before process(). Cheap: stores targets only.
        Returns true if the latency changed and the host needs telling. */
    bool setParams (const Params&) noexcept;

    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    /** For the display. Channel 0 only -- the analysis is per-channel but the
        plot only has room for one, and this is a monophonic-source plugin. */
    void copyDisplayFrame (FormantShifter::DisplayFrame& out) const
    {
        shifters[0].copyDisplayFrame (out);
    }

    double getSampleRate() const noexcept { return sampleRate; }

    /** Window the shifter is actually running, after the tracking setting has
        been scaled to the host's rate and rounded to a power of two. */
    int getWindow() const noexcept { return shifters[0].getWindow(); }

    static constexpr int kMaxChannels = 2;

private:
    /** Tracking settings are quoted at 48 kHz; at other rates the *time* is what
        should stay put, not the sample count, or the envelope estimator's
        frequency resolution would change with the session rate. Rounded to a
        power of two for the radix-2 transform. */
    int windowForRate (Tracking) const noexcept;

    std::array<FormantShifter, kMaxChannels> shifters;

    // Delay-matched dry path, one ring per channel.
    std::array<std::vector<float>, kMaxChannels> dryDelay;
    int dryWrite = 0;
    int dryLength = 0;

    std::vector<float> wetScratch;

    double sampleRate = 48000.0;
    int latencySamples = 0;
    int preparedChannels = 0;
    Tracking currentTracking = Tracking::normal;

    Smoother mix, trim;

    // The first setParams() after prepare() snaps rather than glides: a plugin
    // being loaded with a stored state should already be at that state, not
    // audibly sliding toward it from the defaults.
    bool freshlyPrepared = true;
};

} // namespace vellum
