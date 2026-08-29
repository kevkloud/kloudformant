#pragma once

#include "PluginProcessor.h"
#include "gui/Controls.h"
#include "gui/SpectrumDisplay.h"
#include <memory>

/** The panel.

    Shift is the plugin, so it gets a knob twice the size of anything else and
    sits alone. Everything to its right exists to keep it sounding natural --
    the two pivots that decide which part of the spectrum is allowed to move,
    the consonant protection, and the estimator's resolution -- grouped under
    "Natural" because that is what they are for, and defaulted so that nobody
    has to touch them.

    The finish is FrostyEQ's, which is Ableton's: flat, no bevels, value arcs,
    and a dark well for the analyser.
*/
class VellumAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit VellumAudioProcessorEditor (VellumAudioProcessor&);
    ~VellumAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void drawSection (juce::Graphics&, juce::Rectangle<int>, const juce::String& caption) const;

    VellumAudioProcessor& processorRef;
    vellum::gui::VellumLookAndFeel lookAndFeel;

    vellum::gui::SpectrumDisplay analyser;

    vellum::gui::LabelledKnob shift;
    vellum::gui::LabelledKnob lowPivot, highPivot, consonants, resolution;
    vellum::gui::LabelledKnob mix, trim;

    vellum::gui::SwitchButton levelMatch, bypass;

    juce::ComboBox trackingChooser;
    juce::Label trackingCaption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> trackingAttachment;

    vellum::gui::LevelMeter inputMeter, outputMeter;

    juce::Rectangle<int> shiftSection, naturalSection, outputSection;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VellumAudioProcessorEditor)
};
