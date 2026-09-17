#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/BiasModulatedTremolo.h"

#include <vector>

// Blackface-style bias-modulated tremolo for the rack (see
// dsp/BiasModulatedTremolo) - an LFO shifts a tube's grid bias directly
// rather than just multiplying the output by an amplitude envelope, on
// its own so it can go anywhere in a hand-built chain.
class BiasTremoloPedal : public Pedal
{
public:
    BiasTremoloPedal();

    juce::String getName() const override { return "Bias Tremolo"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<BiasModulatedTremolo> tremolos;

    PedalParameter rate  { "Rate",  0.5f, 10.0f, 5.0f };
    PedalParameter depth { "Depth", 0.0f, 100.0f, 50.0f };
};
