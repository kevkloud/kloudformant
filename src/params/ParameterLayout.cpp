#include "ParameterLayout.h"

namespace vellum::params
{

namespace
{
    juce::String hzText (float value, int)
    {
        if (value >= 1000.0f)
            return juce::String (value / 1000.0f, value < 10000.0f ? 2 : 1) + " kHz";

        return juce::String (juce::roundToInt (value)) + " Hz";
    }

    /** Frequency controls are laid out logarithmically, so a pivot at 200 Hz is
        as far from 100 as 4 kHz is from 2 -- which is how the ear reads them and
        how the warp itself is defined. */
    juce::NormalisableRange<float> logRange (float low, float high)
    {
        juce::NormalisableRange<float> range { low, high };

        range.setSkewForCentre (std::sqrt (low * high));
        return range;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout create()
{
    using Attributes = juce::AudioParameterFloatAttributes;
    using BoolAttributes = juce::AudioParameterBoolAttributes;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Semitones rather than a ratio: it is the unit every other formant control
    // in a studio uses, and it makes "down a fifth" a thing you can dial rather
    // than compute. Continuous rather than stepped: a 0.01 snap interval is not
    // exactly representable in float, and NormalisableRange::snapToLegalValue
    // rounds the default (shift = 0) to -2.68e-7 instead of 0.0 -- silently
    // reintroducing the shift this plugin exists to prove is inaudible at zero.
    // Display is still rounded to two decimals via the string function below.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kShift, kVersionHint }, "Shift",
        juce::NormalisableRange<float> { -12.0f, 12.0f }, 0.0f,
        Attributes {}.withLabel ("st")
                     .withStringFromValueFunction ([] (float v, int)
                     {
                         return (v > 0.0f ? "+" : "") + juce::String (v, 2);
                     })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kLowPivot, kVersionHint }, "Low Pivot",
        logRange (20.0f, 500.0f), 120.0f,
        Attributes {}.withStringFromValueFunction (hzText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kHighPivot, kVersionHint }, "High Pivot",
        logRange (2000.0f, 20000.0f), 6000.0f,
        Attributes {}.withStringFromValueFunction (hzText)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kConsonants, kVersionHint }, "Consonants",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 75.0f,
        Attributes {}.withLabel ("%")
                     .withStringFromValueFunction ([] (float v, int)
                     {
                         return juce::String (juce::roundToInt (v));
                     })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kResolution, kVersionHint }, "Resolution",
        juce::NormalisableRange<float> { 0.30f, 0.90f, 0.01f }, 0.55f,
        Attributes {}.withStringFromValueFunction ([] (float v, int)
                     {
                         return juce::String (v, 2);
                     })));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kTracking, kVersionHint }, "Tracking",
        juce::StringArray { "Fast", "Normal", "Smooth" }, 1));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { kLevelMatch, kVersionHint }, "Level Match", true));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kMix, kVersionHint }, "Mix",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f,
        Attributes {}.withLabel ("%")
                     .withStringFromValueFunction ([] (float v, int)
                     {
                         return juce::String (juce::roundToInt (v));
                     })));

    // Continuous for the same reason as Shift above: a 0.1 interval snaps the
    // 0 dB default a few ULPs off unity gain, which is not "no trim" any more.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kTrim, kVersionHint }, "Trim",
        juce::NormalisableRange<float> { -24.0f, 24.0f }, 0.0f,
        Attributes {}.withLabel ("dB")
                     .withStringFromValueFunction ([] (float v, int)
                     {
                         return (v > 0.0f ? "+" : "") + juce::String (v, 1);
                     })));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { kBypass, kVersionHint }, "Bypass", false,
        BoolAttributes {}));

    return layout;
}

} // namespace vellum::params
