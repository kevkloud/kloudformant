#include "PluginEditor.h"

using namespace vellum;

namespace
{
    constexpr int kWidth = 620, kHeight = 430;
    constexpr int kMargin = 12, kSectionPad = 8, kCaptionHeight = 16;
    constexpr int kAnalyserHeight = 190;
}

VellumAudioProcessorEditor::VellumAudioProcessorEditor (VellumAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      analyser (p),
      shift      (p.getApvts(), params::kShift,      "Shift",       true),
      lowPivot   (p.getApvts(), params::kLowPivot,   "Low Pivot",   false),
      highPivot  (p.getApvts(), params::kHighPivot,  "High Pivot",  false),
      consonants (p.getApvts(), params::kConsonants, "Consonants",  false),
      resolution (p.getApvts(), params::kResolution, "Resolution",  false),
      mix        (p.getApvts(), params::kMix,        "Mix",         false),
      trim       (p.getApvts(), params::kTrim,       "Trim",        true),
      levelMatch (p.getApvts(), params::kLevelMatch, "LEVEL MATCH"),
      bypass     (p.getApvts(), params::kBypass,     "BYPASS"),
      inputMeter  ("IN",  [&p] { return juce::jmax (p.getInputPeak (0),  p.getInputPeak (1)); }),
      outputMeter ("OUT", [&p] { return juce::jmax (p.getOutputPeak (0), p.getOutputPeak (1)); })
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (analyser);

    for (auto* knob : { &shift, &lowPivot, &highPivot, &consonants, &resolution, &mix, &trim })
        addAndMakeVisible (*knob);

    // Shift is the plugin. Everything else is in service of it.
    shift.setKnobDiameter (76);

    for (auto* toggle : { &levelMatch, &bypass })
        addAndMakeVisible (*toggle);

    trackingCaption.setText ("TRACKING", juce::dontSendNotification);
    trackingCaption.setJustificationType (juce::Justification::centred);
    trackingCaption.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (trackingCaption);

    trackingChooser.addItemList ({ "Fast", "Normal", "Smooth" }, 1);
    trackingChooser.setColour (juce::ComboBox::backgroundColourId, theme::panelDeep);
    trackingChooser.setColour (juce::ComboBox::outlineColourId, theme::outline);
    trackingChooser.setColour (juce::ComboBox::textColourId, theme::text);
    addAndMakeVisible (trackingChooser);

    trackingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        p.getApvts(), params::kTracking, trackingChooser);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    setSize (kWidth, kHeight);
}

VellumAudioProcessorEditor::~VellumAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void VellumAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (kMargin);

    bounds.removeFromTop (18);                 // title strip
    analyser.setBounds (bounds.removeFromTop (kAnalyserHeight));
    bounds.removeFromTop (kMargin);

    auto meters = bounds.removeFromRight (54);
    inputMeter.setBounds  (meters.removeFromTop (meters.getHeight() / 2).reduced (4, 0));
    outputMeter.setBounds (meters.reduced (4, 0));

    bounds.removeFromRight (kMargin);

    // Shift on the left at its own size, the naturalness controls in the
    // middle, output on the right.
    shiftSection = bounds.removeFromLeft (118);
    bounds.removeFromLeft (kMargin);

    outputSection = bounds.removeFromRight (128);
    bounds.removeFromLeft (kMargin);
    naturalSection = bounds;

    const auto place = [] (juce::Rectangle<int> cell, gui::LabelledKnob& knob)
    {
        knob.setBounds (cell.withSizeKeepingCentre (
            juce::jmin (cell.getWidth(), 86), knob.getPreferredHeight()));
    };

    {
        auto inner = shiftSection.reduced (kSectionPad);
        inner.removeFromTop (kCaptionHeight);
        place (inner.removeFromTop (shift.getPreferredHeight()), shift);
        inner.removeFromTop (6);
        bypass.setBounds (inner.removeFromTop (20).reduced (6, 0));
    }

    {
        auto inner = naturalSection.reduced (kSectionPad);
        inner.removeFromTop (kCaptionHeight);

        auto row = inner.removeFromTop (lowPivot.getPreferredHeight());
        const auto cell = row.getWidth() / 4;

        place (row.removeFromLeft (cell), lowPivot);
        place (row.removeFromLeft (cell), highPivot);
        place (row.removeFromLeft (cell), consonants);
        place (row, resolution);

        inner.removeFromTop (8);

        auto footer = inner.removeFromTop (20);
        trackingCaption.setBounds (footer.removeFromLeft (62));
        trackingChooser.setBounds (footer.removeFromLeft (86));
        footer.removeFromLeft (10);
        levelMatch.setBounds (footer.removeFromLeft (96));
    }

    {
        auto inner = outputSection.reduced (kSectionPad);
        inner.removeFromTop (kCaptionHeight);

        auto row = inner.removeFromTop (mix.getPreferredHeight());
        place (row.removeFromLeft (row.getWidth() / 2), mix);
        place (row, trim);
    }
}

//==============================================================================
void VellumAudioProcessorEditor::drawSection (juce::Graphics& g, juce::Rectangle<int> area,
                                              const juce::String& caption) const
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (area.toFloat(), theme::corner);

    g.setColour (theme::outline);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), theme::corner, 1.0f);

    g.setColour (theme::textDim);
    g.setFont (theme::labelFont (10.0f));
    g.drawText (caption, area.reduced (kSectionPad, 6).removeFromTop (kCaptionHeight),
                juce::Justification::topLeft, false);
}

void VellumAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    auto title = getLocalBounds().reduced (kMargin).removeFromTop (18);

    g.setColour (theme::text);
    g.setFont (theme::labelFont (13.0f));
    g.drawText ("VELLUM", title, juce::Justification::topLeft, false);

    g.setColour (theme::textDim);
    g.setFont (theme::labelFont (10.0f));
    g.drawText ("formant shifter", title.withTrimmedLeft (62), juce::Justification::topLeft, false);

    drawSection (g, shiftSection,   "SHIFT");
    drawSection (g, naturalSection, "NATURAL");
    drawSection (g, outputSection,  "OUTPUT");
}
