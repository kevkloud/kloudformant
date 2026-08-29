#include "Controls.h"

namespace kloudformant::gui
{

//==============================================================================
LabelledKnob::LabelledKnob (juce::AudioProcessorValueTreeState& state,
                            const juce::String& parameterId,
                            const juce::String& captionText,
                            bool bipolar)
    : caption (captionText)
{
    parameter = state.getParameter (parameterId);
    jassert (parameter != nullptr);

    knob.setBipolar (bipolar);

    // A choice parameter gets detent marks rather than a continuous arc.
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter))
    {
        knob.setDetents (choice->choices.size());
        knob.setSliderSnapsToMousePosition (false);
    }

    addAndMakeVisible (knob);

    readout.setJustificationType (juce::Justification::centredTop);
    readout.setColour (juce::Label::textColourId, theme::textDim);
    readout.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (readout);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, parameterId, knob);

    startTimerHz (20);
    timerCallback();
}

void LabelledKnob::timerCallback()
{
    if (parameter == nullptr)
        return;

    // Ask the parameter for its own text: stepped selectors then read as
    // frequencies, and the high-pass follows the active model.
    const auto text = parameter->getCurrentValueAsText();

    if (text != lastText)
    {
        lastText = text;
        readout.setText (text, juce::dontSendNotification);
    }
}

void LabelledKnob::setKnobEnabled (bool shouldBeEnabled)
{
    knob.setEnabled (shouldBeEnabled);
    readout.setColour (juce::Label::textColourId,
                       shouldBeEnabled ? theme::textDim : theme::textDim.withAlpha (0.4f));
    repaint();
}

void LabelledKnob::paint (juce::Graphics& g)
{
    g.setColour (knob.isEnabled() ? theme::text : theme::textDim.withAlpha (0.5f));
    g.setFont (theme::labelFont (11.0f));
    g.drawText (caption, getLocalBounds().removeFromTop (14),
                juce::Justification::centred, false);
}

void LabelledKnob::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (captionHeight);

    const auto size = juce::jmin (diameter, area.getWidth(), area.getHeight() - readoutHeight);

    auto knobRow = area.removeFromTop (juce::jmax (size, 0));
    knob.setBounds (knobRow.withSizeKeepingCentre (size, size));

    readout.setBounds (area.removeFromTop (readoutHeight));
}

//==============================================================================
SwitchButton::SwitchButton (juce::AudioProcessorValueTreeState& state,
                            const juce::String& parameterId,
                            const juce::String& text)
{
    button.setButtonText (text);
    addAndMakeVisible (button);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state, parameterId, button);
}

void SwitchButton::resized()
{
    button.setBounds (getLocalBounds());
}

void SwitchButton::setSwitchEnabled (bool shouldBeEnabled)
{
    button.setEnabled (shouldBeEnabled);
}

//==============================================================================
LevelMeter::LevelMeter (juce::String captionText, std::function<float()> levelSource)
    : caption (std::move (captionText)), source (std::move (levelSource))
{
    startTimerHz (30);
}

void LevelMeter::timerCallback()
{
    const auto level = source ? source() : 0.0f;

    // Instant attack, gentle release -- peaks stay readable.
    displayed = level > displayed ? level : displayed * 0.82f + level * 0.18f;

    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto full = getLocalBounds();
    const auto labelArea = full.removeFromBottom (11);
    const auto bounds = full.toFloat();

    g.setColour (theme::textDim);
    g.setFont (theme::labelFont (9.0f));
    g.drawText (caption, labelArea, juce::Justification::centred, false);

    g.setColour (theme::panelDeep);
    g.fillRoundedRectangle (bounds, 2.0f);

    // -60 dBFS at the bottom, 0 dBFS at the top.
    const auto db = juce::Decibels::gainToDecibels (displayed, -60.0f);
    const auto norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);

    if (norm > 0.001f)
    {
        auto bar = bounds.reduced (1.0f);
        bar = bar.removeFromBottom (bar.getHeight() * norm);

        g.setColour (db > -1.0f ? theme::meterClip
                   : db > -9.0f ? theme::meterHigh
                                : theme::meterLow);
        g.fillRoundedRectangle (bar, 1.5f);
    }

    g.setColour (theme::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 2.0f, 1.0f);
}

} // namespace kloudformant::gui
