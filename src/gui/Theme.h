#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vellum::theme
{

/** Palette and metrics.

    The same finish as FrostyEQ, which is Ableton's own: flat, restrained, no
    bevels, controls drawn as thin value arcs, a dark well for the analyser.
    Two plugins from the same house should look like it.

    The one departure is the accent. FrostyEQ's cyan reads as "this is doing
    something"; here the default state is doing nothing at all, so the shifted
    envelope gets the warm accent and everything else -- the input spectrum, the
    measured envelope -- stays neutral. When the two envelope curves sit exactly
    on top of each other, the plugin is transparent, and the panel should say so
    without being read.
*/

inline const juce::Colour background   { 0xff303030 };
inline const juce::Colour panel        { 0xff383838 };
inline const juce::Colour panelDeep    { 0xff1a1a1a };   // analyser well
inline const juce::Colour outline      { 0xff4a4a4a };
inline const juce::Colour grid         { 0xff2b2b2b };
inline const juce::Colour gridEmphasis { 0xff3d3d3d };

inline const juce::Colour text         { 0xffd8d8d8 };
inline const juce::Colour textDim      { 0xff8c8c8c };

inline const juce::Colour accent       { 0xffe0a44f };   // the shifted envelope
inline const juce::Colour accentSoft   { 0x33e0a44f };
inline const juce::Colour envelope     { 0xff9fb8c8 };   // the measured envelope
inline const juce::Colour spectrum     { 0x40b0b0b0 };   // the input, behind both
inline const juce::Colour active       { 0xffe8b84b };   // engaged toggles
inline const juce::Colour inactive     { 0xff4f4f4f };

inline const juce::Colour voiced       { 0xff7fbf5f };
inline const juce::Colour meterLow     { 0xff7fbf5f };
inline const juce::Colour meterHigh    { 0xffe8b84b };
inline const juce::Colour meterClip    { 0xffe05a4a };

inline constexpr float knobTrack = 3.0f;
inline constexpr float knobValue = 3.5f;
inline constexpr float corner    = 3.0f;

inline juce::Font labelFont (float height)
{
    return juce::Font (juce::FontOptions {}.withHeight (height));
}

} // namespace vellum::theme
