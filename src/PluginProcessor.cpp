#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace vellum;

namespace
{
    /** Tracking is the only parameter that changes the frame size, and so the
        only one that can change the reported latency. */
    constexpr auto kLatencyAffecting = params::kTracking;
}

VellumAudioProcessor::VellumAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VELLUM", params::create())
{
    shiftParam      = apvts.getRawParameterValue (params::kShift);
    lowPivotParam   = apvts.getRawParameterValue (params::kLowPivot);
    highPivotParam  = apvts.getRawParameterValue (params::kHighPivot);
    consonantsParam = apvts.getRawParameterValue (params::kConsonants);
    resolutionParam = apvts.getRawParameterValue (params::kResolution);
    trackingParam   = apvts.getRawParameterValue (params::kTracking);
    levelMatchParam = apvts.getRawParameterValue (params::kLevelMatch);
    mixParam        = apvts.getRawParameterValue (params::kMix);
    trimParam       = apvts.getRawParameterValue (params::kTrim);
    bypassParam     = apvts.getRawParameterValue (params::kBypass);

    apvts.addParameterListener (kLatencyAffecting, this);
}

VellumAudioProcessor::~VellumAudioProcessor()
{
    apvts.removeParameterListener (kLatencyAffecting, this);
}

//==============================================================================
void VellumAudioProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    sampleRateForTail.store (sampleRate, std::memory_order_relaxed);

    dsp.prepare (sampleRate, maximumExpectedSamplesPerBlock,
                 juce::jmax (1, getTotalNumInputChannels()));

    dsp.setParams (currentParams());

    reportedLatency.store (dsp.getLatencySamples(), std::memory_order_relaxed);
    setLatencySamples (dsp.getLatencySamples());
}

void VellumAudioProcessor::releaseResources()
{
    dsp.reset();
}

double VellumAudioProcessor::getTailLengthSeconds() const
{
    const auto rate = sampleRateForTail.load (std::memory_order_relaxed);

    return rate > 0.0 ? (double) dsp.getLatencySamples() / rate : 0.0;
}

bool VellumAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
DspCore::Params VellumAudioProcessor::currentParams() const noexcept
{
    DspCore::Params p;

    p.shiftSemitones = shiftParam->load (std::memory_order_relaxed);
    p.lowPivotHz     = lowPivotParam->load (std::memory_order_relaxed);
    p.highPivotHz    = highPivotParam->load (std::memory_order_relaxed);
    p.consonants     = consonantsParam->load (std::memory_order_relaxed);
    p.resolution     = resolutionParam->load (std::memory_order_relaxed);
    p.mixPercent     = mixParam->load (std::memory_order_relaxed);
    p.trimDb         = trimParam->load (std::memory_order_relaxed);
    p.levelMatch     = levelMatchParam->load (std::memory_order_relaxed) > 0.5f;
    p.bypass         = bypassParam->load (std::memory_order_relaxed) > 0.5f;
    p.tracking       = (Tracking) juce::jlimit (0, 2,
                           (int) trackingParam->load (std::memory_order_relaxed));

    return p;
}

void VellumAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numInputs  = getTotalNumInputChannels();
    const auto numOutputs = getTotalNumOutputChannels();

    for (int ch = numInputs; ch < numOutputs; ++ch)
        buffer.clear (ch, 0, numSamples);

    const auto channels = juce::jmin (numInputs, numOutputs, DspCore::kMaxChannels);

    if (channels <= 0 || numSamples <= 0)
        return;

    for (int ch = 0; ch < channels; ++ch)
        inputPeak[(size_t) ch].store (buffer.getMagnitude (ch, 0, numSamples),
                                      std::memory_order_relaxed);

    if (dsp.setParams (currentParams()))
        latencyNeedsPublishing.store (true, std::memory_order_relaxed);

    dsp.process (buffer.getArrayOfWritePointers(), channels, numSamples);

    for (int ch = 0; ch < channels; ++ch)
        outputPeak[(size_t) ch].store (buffer.getMagnitude (ch, 0, numSamples),
                                       std::memory_order_relaxed);

    // Any channel the DSP did not touch (mono in, stereo out) would otherwise
    // still hold the untreated input.
    for (int ch = channels; ch < numOutputs; ++ch)
        buffer.copyFrom (ch, 0, buffer, channels - 1, 0, numSamples);

    if (latencyNeedsPublishing.load (std::memory_order_relaxed))
        triggerAsyncUpdate();
}

//==============================================================================
void VellumAudioProcessor::parameterChanged (const juce::String&, float)
{
    // The frame size only actually changes inside DspCore::setParams, on the
    // audio thread, so the host is told from handleAsyncUpdate once that has
    // happened -- not from here, where the new latency is not yet known.
    latencyNeedsPublishing.store (true, std::memory_order_relaxed);
    triggerAsyncUpdate();
}

void VellumAudioProcessor::handleAsyncUpdate()
{
    const auto latency = dsp.getLatencySamples();

    if (latency != reportedLatency.exchange (latency, std::memory_order_relaxed))
        setLatencySamples (latency);

    latencyNeedsPublishing.store (false, std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* VellumAudioProcessor::createEditor()
{
    return new VellumAudioProcessorEditor (*this);
}

void VellumAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", params::kStateVersion, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VellumAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VellumAudioProcessor();
}
