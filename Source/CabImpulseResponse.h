#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Procedurally synthesizes cabinet + mic + room impulse responses for use
// with juce::dsp::Convolution. These are NOT captured real-world impulse
// responses - they're built from decaying resonant modes approximating a
// cab/mic's behaviour - but they run through a real convolution engine,
// so a genuine captured IR .wav can drop straight in later (or be loaded
// by the user right now) with no other code changes.
namespace CabImpulseResponse
{
    enum CabType
    {
        cabFourByTwelveV30 = 0,
        cabFourByTwelveGreenback = 1,
        cabTwoByTwelveOpenBack = 2,
        cabOneByTwelveCombo = 3,
        numCabTypes = 4
    };

    enum MicType
    {
        micDynamic = 0, // SM57-style: presence peak, tight low end, aggressive
        micRibbon = 1,  // smoother, darker top end, fuller low-mid, warmer
        numMicTypes = 2
    };

    // One mic's-eye view of a cab: cabType picks the speaker/cabinet's own
    // resonant character, micType colors it the way a mic's own frequency
    // response would, and positionFraction (0=capsule aimed at the cone's
    // edge, 1=aimed dead-center) continuously blends between the brighter
    // edge character and the darker, boomier center character - a real,
    // audible axis, not just a label.
    juce::AudioBuffer<float> generateMicIR(double sampleRate, int cabType, int micType, float positionFraction);

    // A distant room mic's take on the same cabinet: a handful of soft,
    // spaced-out reflections plus high-frequency air damping, layered
    // under the cab's own tone - this is deliberately not a full reverb
    // algorithm (see dsp/SpringReverb for that); it only needs to sound
    // like "the same cab, through more room" when blended in lightly.
    // isStudio selects a longer, smoother/treated-room character; false
    // (Live) gives a tighter, boxier small-stage/rehearsal-room character.
    juce::AudioBuffer<float> generateRoomIR(double sampleRate, bool isStudio);
}
