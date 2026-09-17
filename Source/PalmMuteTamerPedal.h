#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/PalmMuteTamer.h"

#include <vector>

// A single-band dynamic EQ locked to the 120-180Hz palm-mute pocket for
// the rack (see dsp/PalmMuteTamer) - only pulls that band down when a
// hot, percussive chug actually hits it, leaving everything else (and
// quiet playing in that same range) untouched. Placed anywhere in your
// own chain, same as the other Lab/Pedals-tab building blocks - typically
// useful right before or after your gain/distortion stage(s), wherever
// the low-mid buildup is worst in your particular chain.
class PalmMuteTamerPedal : public Pedal
{
public:
    PalmMuteTamerPedal();

    juce::String getName() const override { return "Palm Mute Tamer"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<PalmMuteTamer> tamers;

    PedalParameter amount      { "Amount",      0.0f, 100.0f, 70.0f };
    PedalParameter sensitivity { "Sensitivity", 0.0f, 100.0f, 50.0f };
    PedalParameter level       { "Level",       0.0f, 150.0f, 100.0f };
};
