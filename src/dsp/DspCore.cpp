#include "DspCore.h"

namespace vellum
{

namespace
{
    float decibelsToGain (float db) noexcept
    {
        return std::pow (10.0f, db * 0.05f);
    }
}

int DspCore::windowForRate (Tracking t) const noexcept
{
    const auto scaled = (double) nominalWindowFor (t) * sampleRate / 48000.0;

    int window = 256;

    while (window < 16384 && (double) window * 1.4142135623730951 < scaled)
        window *= 2;

    return window;
}

void DspCore::prepare (double rate, int /*maxBlockSize*/, int numChannels)
{
    sampleRate = rate;
    preparedChannels = std::clamp (numChannels, 1, kMaxChannels);

    // Sized for the longest window any tracking setting can select at this
    // rate, so switching tracking mid-session never allocates.
    const auto maxWindow = windowForRate (Tracking::smooth);
    const auto window = windowForRate (currentTracking);

    for (auto& shifter : shifters)
        shifter.prepare (sampleRate, window, maxWindow);

    latencySamples = shifters[0].getLatencySamples();

    dryLength = maxWindow + 1;

    for (auto& ring : dryDelay)
        ring.assign ((size_t) dryLength, 0.0f);

    // Control-rate smoothing: fast enough to feel immediate on a fader move,
    // slow enough that automation cannot step the gain.
    mix.prepare (sampleRate, 20.0);
    trim.prepare (sampleRate, 20.0);

    reset();
}

void DspCore::reset() noexcept
{
    for (auto& shifter : shifters)
        shifter.reset();

    for (auto& ring : dryDelay)
        std::fill (ring.begin(), ring.end(), 0.0f);

    dryWrite = 0;

    mix.snap (1.0f);
    trim.snap (1.0f);

    freshlyPrepared = true;
}

//==============================================================================
bool DspCore::setParams (const Params& p) noexcept
{
    auto latencyChanged = false;

    if (p.tracking != currentTracking)
    {
        currentTracking = p.tracking;

        const auto window = windowForRate (currentTracking);

        for (auto& shifter : shifters)
            shifter.setWindow (window);

        const auto updated = shifters[0].getLatencySamples();
        latencyChanged = updated != latencySamples;
        latencySamples = updated;
    }

    FormantShifter::Settings s;
    s.ratio = p.bypass ? 1.0 : std::pow (2.0, (double) p.shiftSemitones / 12.0);
    s.lowPivotHz = (double) p.lowPivotHz;
    s.highPivotHz = (double) p.highPivotHz;
    s.consonantPreservation = (double) p.consonants * 0.01;
    s.orderFraction = (double) p.resolution;
    s.levelMatch = p.levelMatch && ! p.bypass;

    for (auto& shifter : shifters)
        shifter.setSettings (s);

    mix.setTarget (p.bypass ? 0.0f : p.mixPercent * 0.01f);
    trim.setTarget (p.bypass ? 1.0f : decibelsToGain (p.trimDb));

    if (freshlyPrepared)
    {
        mix.snap (p.bypass ? 0.0f : p.mixPercent * 0.01f);
        trim.snap (p.bypass ? 1.0f : decibelsToGain (p.trimDb));
        freshlyPrepared = false;
    }

    return latencyChanged;
}

void DspCore::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    const auto active = std::clamp (numChannels, 1, preparedChannels);

    if ((int) wetScratch.size() < numSamples)
        wetScratch.resize ((size_t) numSamples);

    // The dry ring is shared across channels by interleaving reads per channel,
    // so the write pointer advances once per sample, not once per channel.
    const auto startWrite = dryWrite;

    for (int ch = 0; ch < active; ++ch)
    {
        auto* data = channels[ch];
        auto& ring = dryDelay[(size_t) ch];

        // Stash the dry copy before the shifter overwrites the buffer in place.
        auto write = startWrite;

        for (int n = 0; n < numSamples; ++n)
        {
            ring[(size_t) write] = data[n];
            write = write + 1 < dryLength ? write + 1 : 0;
        }

        std::copy_n (data, numSamples, wetScratch.begin());
        shifters[(size_t) ch].process (wetScratch.data(), numSamples);

        // Read the dry path back out delayed by exactly the shifter's latency,
        // so mix is a crossfade rather than a comb filter.
        auto read = startWrite - latencySamples;

        while (read < 0)
            read += dryLength;

        auto localMix = mix;
        auto localTrim = trim;

        for (int n = 0; n < numSamples; ++n)
        {
            const auto dry = ring[(size_t) read];
            read = read + 1 < dryLength ? read + 1 : 0;

            const auto m = localMix.tick();
            data[n] = (dry + m * (wetScratch[(size_t) n] - dry)) * localTrim.tick();
        }

        // Every channel must advance the smoothers identically, so they are
        // ticked on a local copy and committed once.
        if (ch == active - 1)
        {
            mix = localMix;
            trim = localTrim;
        }
    }

    dryWrite = (startWrite + numSamples) % dryLength;
}

} // namespace vellum
