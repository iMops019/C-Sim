#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>

#include "PedalParameter.h"

// Base interface for anything that can sit in the signal chain.
class Pedal
{
public:
    virtual ~Pedal() = default;

    virtual juce::String getName() const = 0;

    virtual void prepare(double sampleRate, int maximumBlockSize, int numChannels) = 0;

    // Processes in place. channelData/numChannels describe the active
    // output channels of the current audio block.
    virtual void process(float* const* channelData, int numChannels, int numSamples) = 0;

    // Knob-controllable parameters, for the UI to build controls from.
    // Empty by default - override if the pedal has any.
    virtual std::vector<PedalParameter*> getParameters() { return {}; }

    std::atomic<bool> bypassed { false };
};
