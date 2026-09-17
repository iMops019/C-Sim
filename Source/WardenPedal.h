#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

#include "Pedal.h"
#include "dsp/WardenStage.h"

// Models the EarthQuaker Devices Warden - an optical feedback-style
// compressor with a full independent control set (Sustain, Ratio,
// Attack, Release, Tone, Level), matching the real pedal exactly. Its
// independently adjustable Attack/Release are what set it apart from
// this toolkit's other compressor (DynaCompStage's fixed single time
// constant) - a slow Attack lets a tapped note's transient speak before
// compression clamps down, the real reason this pedal suits
// tapping/mathrock articulation better than a simpler comp. See
// dsp/WardenStage for the researched circuit facts and compression
// model.
class WardenPedal : public Pedal
{
public:
    WardenPedal();

    juce::String getName() const override { return "The Warden"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<WardenStage> stages;

    PedalParameter sustain { "Sustain", 0.0f, 100.0f, 45.0f };
    PedalParameter ratio   { "Ratio",   0.0f, 100.0f, 40.0f };
    PedalParameter attack  { "Attack",  0.0f, 100.0f, 20.0f };
    PedalParameter release { "Release", 0.0f, 100.0f, 30.0f };
    PedalParameter tone    { "Tone",    0.0f, 100.0f, 50.0f };
    PedalParameter level   { "Level",   0.0f, 100.0f, 65.0f };
};
