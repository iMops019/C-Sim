#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Procedurally synthesizes cabinet impulse responses for use with
// juce::dsp::Convolution. These are NOT captured real-world impulse
// responses - they're built from decaying resonant modes approximating a
// cab's behaviour - but they run through a real convolution engine, so a
// genuine captured IR .wav can drop straight in later with no other code
// changes.
namespace CabImpulseResponse
{
    // A Vintage-30-loaded 4x12 + mic: tight low end, ~120Hz body
    // resonance, the V30's upper-mid bite around 2.8kHz, steep top-end
    // roll-off. Two channels with a slight offset/phase difference between
    // them, approximating a two-microphone blend for some stereo width.
    juce::AudioBuffer<float> generateFourByTwelveV30(double sampleRate);
}
