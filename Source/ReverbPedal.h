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

    std::vector<PedalParameter*> getParameters() override;

private:
    void updateReverbParameters();

    juce::Reverb reverb;

    PedalParameter mix      { "Mix",     0.0f, 100.0f, 40.0f };
    PedalParameter roomSize { "Room",    0.0f, 100.0f, 50.0f };
    PedalParameter damping  { "Damping", 0.0f, 100.0f, 50.0f };
    PedalParameter level    { "Level",   0.0f, 150.0f, 100.0f };
};
