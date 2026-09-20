#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

#include "Pedal.h"
#include "dsp/MorningGloryStage.h"

// Models the JHS Morning Glory V4 - a transparent, Marshall Bluesbreaker-
// derived overdrive. Knob/toggle set matches the real V4: Gain, a Gain
// Range toggle (Lo/Hi), Boost, Bright Cut, and Volume. See
// dsp/MorningGloryStage for the researched circuit facts and clipping
// model.
class MorningGloryPedal : public Pedal
{
public:
    MorningGloryPedal();

    juce::String getName() const override { return "Morning Glory"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<MorningGloryStage> stages;

    PedalParameter gain      { "Gain",       0.0f, 100.0f, 35.0f };
    PedalParameter gainRange { "Gain Range", 0.0f, 1.0f,   0.0f, { "Lo", "Hi" } };
    PedalParameter boost     { "Boost",      0.0f, 100.0f, 0.0f };
    PedalParameter brightCut { "Bright Cut", 0.0f, 100.0f, 0.0f };
    // Default Volume is gain-staged: a strummed chord comes out ~3 dB louder than the guitar (RMS).
    // At 100 it was 20 dB hotter and put peaks +5 dB over digital full scale.
    PedalParameter volume    { "Volume",     0.0f, 150.0f, 14.0f };
};
