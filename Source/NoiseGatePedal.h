#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/NoiseGate.h"

#include <vector>

// A standard noise gate: envelope-follows the signal and ramps the gain
// down to silence when it drops below Threshold, with a fast open and a
// user-controllable Release. High-gain amps like the 5150 are hissy at
// high Gain between notes - this is what real metal rigs use to tame it.
// Wraps the dsp/ toolkit's NoiseGate (one instance per channel) rather
// than its own copy of the gating math, so this and the toolkit's gate
// (used inside MetalPreampChain/HybridAmp) can't drift apart.
class NoiseGatePedal : public Pedal
{
public:
    NoiseGatePedal();

    juce::String getName() const override { return "Noise Gate"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<NoiseGate> gates;

    PedalParameter threshold { "Thresh",  -80.0f, 0.0f,   -50.0f };
    PedalParameter release   { "Release",  10.0f, 500.0f, 150.0f };
};
