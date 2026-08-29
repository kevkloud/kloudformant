#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "params/ParameterLayout.h"
#include "dsp/DspCore.h"
#include <array>
#include <atomic>

//==============================================================================
/** The host-facing wrapper. Everything that touches audio lives in
    kloudformant::DspCore, which has no JUCE dependency; this class exists to move
    parameter values into it, report latency, and keep the editor supplied.
*/
class KloudFormantAudioProcessor final : public juce::AudioProcessor,
                                   private juce::AudioProcessorValueTreeState::Listener,
                                   private juce::AsyncUpdater
{
public:
    KloudFormantAudioProcessor();
    ~KloudFormantAudioProcessor() override;

    //== AudioProcessor ========================================================
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return JucePlugin_Name; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }

    /** The overlap-add still has a window's worth of signal in it when the
        input stops, and a host that cuts the tail early would clip the end of
        every phrase. */
    double getTailLengthSeconds() const override;

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //== Ours ==================================================================
    juce::AudioProcessorValueTreeState& getApvts() noexcept  { return apvts; }

    /** For the spectrum display. Copies out of the audio thread's published
        analysis; never blocks it. */
    void copyDisplayFrame (kloudformant::FormantShifter::DisplayFrame& out) const
    {
        dsp.copyDisplayFrame (out);
    }

    /** Metering, written by the audio thread and polled by the editor on a
        timer. Publish-and-sample; never push from audio to UI. */
    float getInputPeak  (int ch) const noexcept { return read (inputPeak,  ch); }
    float getOutputPeak (int ch) const noexcept { return read (outputPeak, ch); }

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    kloudformant::DspCore::Params currentParams() const noexcept;

    // Recorded directly in prepareToPlay rather than read back via
    // AudioProcessor::getSampleRate(), which is only populated once the host
    // wrapper has called setRateAndBufferSizeDetails -- a step prepareToPlay
    // does not perform itself, so getTailLengthSeconds must not depend on it.
    std::atomic<double> sampleRateForTail { 0.0 };

    static float read (const std::array<std::atomic<float>, 2>& a, int ch) noexcept
    {
        return a[(size_t) juce::jlimit (0, 1, ch)].load (std::memory_order_relaxed);
    }

    juce::AudioProcessorValueTreeState apvts;

    // Resolved once in the constructor. Looking parameters up by string ID on
    // the audio thread would be a hash lookup per block.
    std::atomic<float>* shiftParam      = nullptr;
    std::atomic<float>* lowPivotParam   = nullptr;
    std::atomic<float>* highPivotParam  = nullptr;
    std::atomic<float>* consonantsParam = nullptr;
    std::atomic<float>* resolutionParam = nullptr;
    std::atomic<float>* trackingParam   = nullptr;
    std::atomic<float>* levelMatchParam = nullptr;
    std::atomic<float>* mixParam        = nullptr;
    std::atomic<float>* trimParam       = nullptr;
    std::atomic<float>* bypassParam     = nullptr;

    kloudformant::DspCore dsp;

    // Latency is only pushed to the host when it actually changes. Automating
    // Tracking otherwise floods the host with setLatencySamples calls on every
    // move, which is enough to destabilise it.
    std::atomic<int> reportedLatency { -1 };
    std::atomic<bool> latencyNeedsPublishing { false };

    std::array<std::atomic<float>, 2> inputPeak  { };
    std::array<std::atomic<float>, 2> outputPeak { };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KloudFormantAudioProcessor)
};
