#pragma once

#include "Theme.h"

namespace kloudformant::gui
{

/** A slider that knows whether its value arc should grow from the centre
    (gain, pan) or from the minimum (level, mix). */
class ArcSlider : public juce::Slider
{
public:
    ArcSlider() : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox) {}

    void setBipolar (bool shouldBeBipolar) noexcept { bipolar = shouldBeBipolar; }
    bool isBipolar() const noexcept                 { return bipolar; }

    /** Stepped selectors draw detent ticks instead of a continuous arc. */
    void setDetents (int count) noexcept            { detents = count; }
    int  getDetents() const noexcept                { return detents; }

private:
    bool bipolar = false;
    int  detents = 0;
};

//==============================================================================
class KloudFormantLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    KloudFormantLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawHighlighted, bool shouldDrawDown) override;

    juce::Font getLabelFont (juce::Label&) override;
};

} // namespace kloudformant::gui
