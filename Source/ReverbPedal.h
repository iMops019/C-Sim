#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"

class ReverbPedal : public Pedal
{
public:
    ReverbPedal();

    juce::String getName() const override { return "Reverb"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

private:
    juce::Reverb reverb;
};
