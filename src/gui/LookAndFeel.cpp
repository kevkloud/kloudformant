#include "LookAndFeel.h"

namespace vellum::gui
{

VellumLookAndFeel::VellumLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::background);
    setColour (juce::Label::textColourId,                 theme::text);
    setColour (juce::TooltipWindow::backgroundColourId,   theme::panel);
    setColour (juce::TooltipWindow::textColourId,         theme::text);
    setColour (juce::TooltipWindow::outlineColourId,      theme::outline);
}

juce::Font VellumLookAndFeel::getLabelFont (juce::Label& label)
{
    return theme::labelFont (label.getHeight() > 0 ? juce::jmin (12.0f, (float) label.getHeight())
                                                   : 11.0f);
}

//==============================================================================
void VellumLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float startAngle, float endAngle,
                                          juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto* arc = dynamic_cast<ArcSlider*> (&slider);
    const auto bipolar = arc != nullptr && arc->isBipolar();
    const auto detents = arc != nullptr ? arc->getDetents() : 0;

    // Stepped controls need room outside the track for their detent marks.
    const auto arcRadius = radius - theme::knobValue * 0.5f - (detents > 0 ? 4.0f : 0.0f);

    const auto angle = startAngle + sliderPos * (endAngle - startAngle);

    // Track.
    {
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             startAngle, endAngle, true);
        g.setColour (theme::outline);
        g.strokePath (track, juce::PathStrokeType (theme::knobTrack, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // Value arc: from the centre for bipolar controls, from the minimum
    // otherwise -- the same convention Live uses.
    if (detents == 0)
    {
        const auto from = bipolar ? startAngle + 0.5f * (endAngle - startAngle) : startAngle;

        if (std::abs (angle - from) > 1.0e-4f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 juce::jmin (from, angle), juce::jmax (from, angle), true);
            g.setColour (theme::accent);
            g.strokePath (value, juce::PathStrokeType (theme::knobValue, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }
    }
    else
    {
        // Stepped selector: mark each switch position, highlight the active one.
        for (int i = 0; i < detents; ++i)
        {
            const auto t = detents > 1 ? (float) i / (float) (detents - 1) : 0.0f;
            const auto a = startAngle + t * (endAngle - startAngle);
            const auto inner = arcRadius + 3.0f;
            const auto outer = arcRadius + (detents > 12 ? 6.0f : 7.0f);

            const juce::Point<float> p1 { centre.x + inner * std::sin (a), centre.y - inner * std::cos (a) };
            const juce::Point<float> p2 { centre.x + outer * std::sin (a), centre.y - outer * std::cos (a) };

            const auto isActive = std::abs (a - angle) < 1.0e-3f;
            g.setColour (isActive ? theme::accent : theme::textDim.withAlpha (0.55f));
            g.drawLine ({ p1, p2 }, isActive ? 2.5f : 1.4f);
        }
    }

    // Pointer.
    {
        const auto tip  = arcRadius - 3.0f;
        const auto tail = arcRadius * 0.35f;

        const juce::Point<float> p1 { centre.x + tail * std::sin (angle), centre.y - tail * std::cos (angle) };
        const juce::Point<float> p2 { centre.x + tip  * std::sin (angle), centre.y - tip  * std::cos (angle) };

        g.setColour (slider.isEnabled() ? theme::text : theme::textDim);
        g.drawLine ({ p1, p2 }, 2.0f);
    }

    if (! slider.isEnabled())
    {
        g.setColour (theme::background.withAlpha (0.55f));
        g.fillEllipse (bounds);
    }
}

//==============================================================================
void VellumLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                          bool shouldDrawHighlighted, bool shouldDrawDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto on = button.getToggleState();

    auto fill = on ? theme::active : theme::inactive;

    if (shouldDrawDown)          fill = fill.darker (0.25f);
    else if (shouldDrawHighlighted) fill = fill.brighter (0.12f);

    if (! button.isEnabled())
        fill = fill.withAlpha (0.35f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, theme::corner);

    g.setColour (theme::outline);
    g.drawRoundedRectangle (bounds, theme::corner, 1.0f);

    g.setColour (on ? juce::Colours::black.withAlpha (0.8f)
                    : (button.isEnabled() ? theme::text : theme::textDim));
    g.setFont (theme::labelFont (10.5f));
    g.drawText (button.getButtonText(), bounds, juce::Justification::centred, false);
}

} // namespace vellum::gui
