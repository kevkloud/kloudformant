#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace vellum::params
{

//==============================================================================
// Parameter IDs.
//
// This is a permanent, append-only schema. Once a user saves an Ableton set,
// automation lanes and stored state are keyed by these exact strings. Renaming
// or reordering silently loses settings in every existing project. Treat a
// change here the way you would treat a wire-protocol change: don't, and if you
// must, add a new ID and migrate on load.
//==============================================================================

inline constexpr auto kShift      = "shift";
inline constexpr auto kLowPivot   = "low_pivot";
inline constexpr auto kHighPivot  = "high_pivot";
inline constexpr auto kConsonants = "consonants";
inline constexpr auto kResolution = "resolution";
inline constexpr auto kTracking   = "tracking";
inline constexpr auto kLevelMatch = "level_match";
inline constexpr auto kMix        = "mix";
inline constexpr auto kTrim       = "trim";
inline constexpr auto kBypass     = "bypass";

/** Bump only when adding parameters; existing entries keep their original hint. */
inline constexpr int kVersionHint  = 1;
inline constexpr int kStateVersion = 1;

juce::AudioProcessorValueTreeState::ParameterLayout create();

} // namespace vellum::params
