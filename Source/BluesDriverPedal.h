#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/BluesDriverStage.h"

#include <vector>

// The Boss BD-2 Blues Driver (Pedals tab), modeled from its service-manual
// schematic - see dsp/BluesDriverStage.h for the circuit, the sources and what is
// calibrated rather than read. Its three knobs are the pedal's own:
//
//   Gain    the ganged 250k pot that sets BOTH discrete gain stages' feedback at
//           once (about +22dB at minimum, ~+50dB at maximum, end to end)
//   Tone    a variable treble cut - not a shelf - so it goes from a full, open top
//           end (clockwise) to dark
//   Level   output level
//
// The BD-2 breaks up early and gently: each gain stage is a discrete amplifier
// with modest open-loop gain that simply runs out of swing, so the drive comes on
// as compression before it turns into clipping - and there is a fixed bump around
// 120-150Hz that gives it its round low end.
class BluesDriverPedal : public Pedal
{
public:
    BluesDriverPedal();

    juce::String getName() const override { return "Boss BD-2 Blues Driver"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<BluesDriverStage> stages;

    PedalParameter gain  { "Gain",  0.0f, 100.0f, 40.0f };
    PedalParameter tone  { "Tone",  0.0f, 100.0f, 55.0f };
    // Default Level is gain-staged: a strummed chord comes out ~3 dB louder than the guitar (RMS).
    // At 65 it was 12 dB hotter.
    PedalParameter level { "Level", 0.0f, 100.0f, 39.0f };
};
