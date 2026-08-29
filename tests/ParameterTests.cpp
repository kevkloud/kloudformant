#include "PluginProcessor.h"

#include <iostream>
#include <string>

namespace
{
    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        if (! ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; }
    }
}

//==============================================================================
/** The parameter schema is a wire protocol: an Ableton set saved today has to
    open the same way in two years. These tests exist to make a rename or a
    reorder fail loudly here rather than silently in someone's project.
*/
static void testSchemaIsStable()
{
    KloudFormantAudioProcessor processor;
    auto& apvts = processor.getApvts();

    // Every ID that has ever shipped must still resolve.
    for (auto id : { kloudformant::params::kShift, kloudformant::params::kLowPivot,
                     kloudformant::params::kHighPivot, kloudformant::params::kConsonants,
                     kloudformant::params::kResolution, kloudformant::params::kTracking,
                     kloudformant::params::kLevelMatch, kloudformant::params::kMix,
                     kloudformant::params::kTrim, kloudformant::params::kBypass })
    {
        check (apvts.getParameter (id) != nullptr,
               std::string ("parameter '") + id + "' exists");
    }
}

static void testDefaultsAreTransparent()
{
    KloudFormantAudioProcessor processor;
    auto& apvts = processor.getApvts();

    // The plugin must open doing nothing. A formant shifter that arrives with a
    // shift dialled in is a formant shifter you cannot trust as an insert.
    check (apvts.getParameter (kloudformant::params::kShift)->getValue()
               == apvts.getParameter (kloudformant::params::kShift)->getDefaultValue(),
           "shift opens at its default");

    check (*apvts.getRawParameterValue (kloudformant::params::kShift) == 0.0f,
           "the default shift is exactly zero");

    check (*apvts.getRawParameterValue (kloudformant::params::kMix) == 100.0f,
           "the default mix is fully wet");
}

static void testStateRoundTrips()
{
    juce::MemoryBlock saved;

    {
        KloudFormantAudioProcessor processor;
        auto& apvts = processor.getApvts();

        apvts.getParameter (kloudformant::params::kShift)->setValueNotifyingHost (0.75f);
        apvts.getParameter (kloudformant::params::kConsonants)->setValueNotifyingHost (0.25f);
        apvts.getParameter (kloudformant::params::kLevelMatch)->setValueNotifyingHost (0.0f);

        processor.getStateInformation (saved);
    }

    KloudFormantAudioProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());

    auto& apvts = restored.getApvts();

    check (std::abs (apvts.getParameter (kloudformant::params::kShift)->getValue() - 0.75f) < 1.0e-4f,
           "shift survives a save and reload");
    check (std::abs (apvts.getParameter (kloudformant::params::kConsonants)->getValue() - 0.25f) < 1.0e-4f,
           "consonants survives a save and reload");
    check (apvts.getParameter (kloudformant::params::kLevelMatch)->getValue() < 0.5f,
           "level match survives a save and reload");
}

static void testLatencyIsReported()
{
    KloudFormantAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    // A spectral process that does not declare its latency drags the whole
    // track out of time, and the host has no way to find out on its own.
    check (processor.getLatencySamples() > 0, "the processor reports a latency");
    check (processor.getTailLengthSeconds() > 0.0, "the processor reports a tail");
}

static void testBusLayouts()
{
    KloudFormantAudioProcessor processor;

    const auto supports = [&] (const juce::AudioChannelSet& set)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (set);
        layout.outputBuses.add (set);
        return processor.isBusesLayoutSupported (layout);
    };

    check (supports (juce::AudioChannelSet::mono()),   "mono is supported");
    check (supports (juce::AudioChannelSet::stereo()), "stereo is supported");
}

static void testProcessesWithoutCrashing()
{
    KloudFormantAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;

    juce::Random random { 42 };

    for (int block = 0; block < 200; ++block)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int n = 0; n < buffer.getNumSamples(); ++n)
                buffer.setSample (ch, n, random.nextFloat() * 0.5f - 0.25f);

        processor.processBlock (buffer, midi);
    }

    auto finite = true;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int n = 0; n < buffer.getNumSamples(); ++n)
            finite = finite && std::isfinite (buffer.getSample (ch, n));

    check (finite, "200 blocks of noise produce finite output");
}

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "KloudFormant parameter tests\n";

    testSchemaIsStable();
    testDefaultsAreTransparent();
    testStateRoundTrips();
    testLatencyIsReported();
    testBusLayouts();
    testProcessesWithoutCrashing();

    if (failures == 0)
        std::cout << "all tests passed\n";
    else
        std::cout << failures << " test(s) failed\n";

    return failures == 0 ? 0 : 1;
}
