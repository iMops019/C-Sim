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
        // Fender-voiced cabs, appended after the existing British/metal
        // ones rather than inserted before them, so a saved preset's
        // numeric Cab index still means what it always meant. Real Fender
        // combos overwhelmingly used Jensen/Oxford/Eminence speakers
        // voiced brighter and less mid-humped than a Celestion Greenback
        // or V30 - extended, chimey top end and a comparatively small
        // midrange bump, the source of the classic "blackface chime."
        cabFenderOneByTen = 4,   // Champ/Princeton-style single 10" - brightest, thinnest, least low end
        cabFenderTwoByTen = 5,   // Super Reverb-style twin 10" - fuller and louder than one 10, still chimey
        cabFenderOneByTwelve = 6, // Deluxe/Twin-style single 12" - Fender's classic full-bodied clean chime
        cabFenderFourByTwelve = 7, // a bigger Fender-voiced cab - the most low end/air of the four, same bright character
        numCabTypes = 8
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

    // generateMicIR with a SECOND speaker type mixed in - a mixed-speaker
    // cab (the classic example: a 4x12 with V30s and Greenbacks together,
    // for the V30's bite plus the Greenback's warmth). `mix01` is the
    // balance the mic hears: 0 = only cabType's speakers, 1 = only
    // secondCabType's, 0.5 = an even blend. Both use the same mic type and
    // position, and are summed in phase - a mic roughly equidistant from
    // the two speakers, so there is no extra arrival-time offset between
    // them (that would be a comb filter on top of the tonal blend; the
    // Distance controls are where arrival time lives).
    //
    // The blend is linear on the IRs themselves, deliberately: what the
    // two speakers share (the on-axis attack transient every table
    // renders in phase) stays at full level, while what they DON'T share
    // (their different resonant modes) simply adds - which is what
    // actually happens when two different speakers are heard together.
    //
    // secondCabType < 0 (or the same as cabType) or mix01 <= 0 returns
    // exactly generateMicIR's result, bit for bit - a cab with no second
    // speaker is completely unchanged.
    juce::AudioBuffer<float> generateMixedMicIR(double sampleRate, int cabType, int secondCabType, float mix01,
                                                 int micType, float positionFraction);

    // How many speakers this cab actually has, for the Inspector's visual
    // cab drawing to lay out the right number of speaker circles - not
    // used by the DSP itself (the resonant-mode model doesn't simulate
    // individual speakers spatially), purely a UI fact about each cab.
    int speakerCount(int cabType);

    // A distant room mic's take on the same cabinet: a handful of soft,
    // spaced-out reflections plus high-frequency air damping, layered
    // under the cab's own tone - this is deliberately not a full reverb
    // algorithm (see dsp/SpringReverb for that); it only needs to sound
    // like "the same cab, through more room" when blended in lightly.
    // isStudio selects a longer, smoother/treated-room character; false
    // (Live) gives a tighter, boxier small-stage/rehearsal-room character.
    juce::AudioBuffer<float> generateRoomIR(double sampleRate, bool isStudio);
}
