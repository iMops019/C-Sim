#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Pedal.h"
#include "dsp/PowerAmpStage.h"

#include <vector>

// A standalone power amp stage for the rack - push-pull, sag, negative
// feedback (see PowerAmpStage) - dropped in wherever you want that
// character in your own hand-built signal chain, rather than only
// inside a fixed preset amp.
class PowerAmpPedal : public Pedal
{
public:
    PowerAmpPedal();

    juce::String getName() const override { return "Power Amp"; }

    void prepare(double sampleRate, int maximumBlockSize, int numChannels) override;
    void process(float* const* channelData, int numChannels, int numSamples) override;

    std::vector<PedalParameter*> getParameters() override;

private:
    std::vector<PowerAmpStage> stages;

    PedalParameter sag      { "Sag",      0.0f, 100.0f, 40.0f };
    PedalParameter feedback { "Feedback", 0.0f, 100.0f, 30.0f };
    PedalParameter level    { "Level",    0.0f, 150.0f, 100.0f };
};
