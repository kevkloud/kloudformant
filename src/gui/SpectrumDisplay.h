#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include "dsp/FormantShifter.h"

class KloudFormantAudioProcessor;

namespace kloudformant::gui
{

/** The analyser: input spectrum behind, the measured envelope over it, and the
    shifted envelope over that.

    This is the panel's main argument. You can watch the estimator refuse to
    track the harmonics -- the envelope stays smooth while the comb underneath it
    moves -- and you can see the exact filter being applied as the distance
    between the two envelope curves. At zero shift they lie on top of each other
    and the plugin is, visibly and audibly, not doing anything.

    Everything drawn here comes from the audio thread's published analysis via a
    copy, so the plot cannot disagree with what is heard and nothing is shared
    across threads.
*/
class SpectrumDisplay final : public juce::Component,
                              private juce::Timer
{
public:
    explicit SpectrumDisplay (KloudFormantAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    float frequencyToX (double hz) const noexcept;
    float decibelsToY  (double db) const noexcept;

    /** Builds a path from a dB-per-bin curve, skipping bins that would land on
        the same pixel column. Below about 200 Hz there are several bins per
        column and above 10 kHz several columns per bin, so drawing every bin is
        both slow and, at the bottom, visibly noisy. */
    void buildPath (juce::Path&, const std::vector<float>& curve,
                    double binWidthHz, float offsetDb) const;

    /** The closed region between the two envelope curves. That region *is* the
        correction filter, so filling it shows at a glance how much is being
        done and where -- and shows nothing at all when the shift is zero. */
    void buildBandPath (juce::Path&, double binWidthHz) const;

    KloudFormantAudioProcessor& processorRef;

    FormantShifter::DisplayFrame frame;

    // The input spectrum is averaged over repaints. A single frame's magnitude
    // is far too jumpy to read; the envelope curves are already smooth and are
    // drawn as they arrive.
    std::vector<float> smoothedInput;

    juce::Path spectrumPath, envelopePath, shiftedPath, bandPath;
    juce::Rectangle<float> plot;

    static constexpr double kMinHz = 50.0, kMaxHz = 18000.0;
    static constexpr double kTopDb = 6.0, kBottomDb = -78.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumDisplay)
};

} // namespace kloudformant::gui
