#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "LookAndFeel.h"
#include <functional>

namespace kloudformant::gui
{

/** Caption above, knob, live value readout below -- the arrangement Live uses
    on its own device controls. Reads its display text straight from the
    parameter, so stepped selectors show "1.6 kHz" rather than an index, and
    the high-pass relabels itself when the model changes. */
class LabelledKnob final : public juce::Component,
                           private juce::Timer
{
public:
    LabelledKnob (juce::AudioProcessorValueTreeState& state,
                  const juce::String& parameterId,
                  const juce::String& captionText,
                  bool bipolar);

    void paint (juce::Graphics&) override;
    void resized() override;

    void setKnobEnabled (bool shouldBeEnabled);

    /** Gain controls are drawn larger than their frequency selectors, echoing
        the hardware's concentric pairs where the gain is the dominant knob. */
    void setKnobDiameter (int px) noexcept { diameter = px; resized(); }

    ArcSlider& getSlider() noexcept { return knob; }

    /** Caption + knob + readout, stacked. Lets the editor size cells to their
        content rather than stretching them down the whole section. */
    int getPreferredHeight() const noexcept { return captionHeight + diameter + readoutHeight; }

    static constexpr int captionHeight = 15;
    static constexpr int readoutHeight = 14;

private:
    void timerCallback() override;

    juce::String caption;
    juce::RangedAudioParameter* parameter = nullptr;

    ArcSlider  knob;
    juce::Label readout;

    juce::String lastText;
    int diameter = 44;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LabelledKnob)
};

//==============================================================================
/** Small rounded-rectangle toggle, lit when engaged. */
class SwitchButton final : public juce::Component
{
public:
    SwitchButton (juce::AudioProcessorValueTreeState& state,
                  const juce::String& parameterId,
                  const juce::String& text);

    void resized() override;
    void setSwitchEnabled (bool shouldBeEnabled);

private:
    juce::ToggleButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwitchButton)
};

//==============================================================================
/** Peak meter. The audio thread publishes a level; this samples it on a timer
    and decays smoothly, so the needle does not flicker at block rate. */
class LevelMeter final : public juce::Component,
                         private juce::Timer
{
public:
    LevelMeter (juce::String caption, std::function<float()> levelSource);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    juce::String caption;
    std::function<float()> source;
    float displayed = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
};

} // namespace kloudformant::gui
