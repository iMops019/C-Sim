#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"

// A standard noise gate: envelope-follows the signal and ramps the gain
// down to silence when it drops below Threshold, with a fast open and a
// user-controllable Release. High-gain amps like the 5150 are hissy at
// high Gain between notes - this is what real metal rigs use to tame it.
class NoiseGatePedal : public Pedal
{
public:
    NoiseGatePedal();

    juce::String getName() const override { return "Noise Gate"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    double currentSampleRate = 44100.0;

    static constexpr int maxChannels = 2;
    float envelope[maxChannels] = { 0.0f, 0.0f };
    float gateGain[maxChannels] = { 1.0f, 1.0f };

    PedalParameter threshold { "Thresh",  -80.0f, 0.0f,   -50.0f };
    PedalParameter release   { "Release",  10.0f, 500.0f, 150.0f };
};
